# RoQueT (Robust Queue Transfer)

## Motivation

The idea behind the `RoQueT` builds upon a buffer of atomic states which indices correspond
to the data buffer. With this approach it is possible to create resilient applications in
an embedded device, e.g. the Kria SOM.

On the Kria one could have an ring buffer implemented via multiple DMA (at minimum 3, to
handle delays on the PS it might be better to have several smaller ones if possible; one for
the PL for immediate use one as unused buffer to continue with the data transfer once the
current one is full and one which is in use by the PS). The R5 could take care of DMA handling,
e.g. getting noticed via Interrupt when one DMA is full, and pass a pointer to the DMA region
to the A53 cores. Since some use cases require always the latest data, e.g. live measurement
data, the queue must have circular behavior and allow to take out the oldest data when the
queue is full. Since this data is associated with a resource, in our example a DMA region,
it needs to be obtained and reused. As long as the A53 cores have the ownership of the DMA
region, the PL is not allowed to use this DMA. This will be managed by the R5 core(s).

To prevent locks from the non-realtime A53 cores, the queue must be lock-free.

Since the R5 cores are 32 bit and the A53 cores are 64 bit, the queue must work on both
architectures. Just in case I have to ever code again on an ADuC842 it would be nice to work
on 8 bit architectures.

In case the application on the A53 core crashes, it must be ensured that the ownership of the
DMA region can still be passed to the R5 core(s). Therefore pushing to the queue must be
handled like database transactions and crashes within the critical sections must be recoverable.
This can be done by using `pending` flags in the state byte and storing `transaction action`
in a persistent memory location alongside with the queue.

The same queue can be used to pass the ownership from the A53 cores to the R5 core(s), one
has just to check whether the queue is full before pushing to it or handle the overflow
situation accordingly.

Since resource leaks (DMA regions) can lead to a failure of the operations (keep in mind, the
measurement devices might run for weeks or months without a power cycle) the queue must protect
against accidental programming errors.

Ideally, data transfer from and to the R5 cores are not only lock-free but even wait-free. This
can also be achieved with the `RoQueT` when it is used in a non-overflowing mode for the return
path (pop from R5 core). The push operation is always wait-free. With some additional work
it might be possible to take more ideas from `BuRiTTO` to make the `pop` also wait-free for
overflowing situations.

# Theory of Operation

## Introduction

The queue consists of a buffer for the data and a buffer for states. Each position of the state
buffer corresponds to a position of the data buffer.

Since the data buffer is not important for the understanding of the operation of the queue
it is omitted from further on. One has to keep in mind that the memory is synchronized by the
atomics in the state buffer and ownership is taken according to the states in the state buffer.

The state is a combination of the following flags
```
END (X)
EMPTY (E)
PENDING (P)
DATA (D)
INSPECTED (I)
OVERFLOW (O) <- might not be required
```

Overview of the diagram used in the document
```
  H = head
  T = tail
  [   ] = state slot
  [ E ] = state slot with empty flags
  [PX ] = state slot with pending and end flags

           H
         [ E ]
    [ E ]     [ X ] T
         [ E ]

  The positions advance clock-wise.
```

The state buffer has this initial states
```
           H
         [ E ]
    [ E ]     [ X ] T
         [ E ]
```
In the initial configuration the tail (write position) is right ahead of the head (read position),
indicating an empty queue.

## Empty condition

The queue is empty when:
- from the point of the producer, the state right ahead of the tail position contains an `E`
- from the point of the consumer, the state at the head position is `E` and the state thereafter
  contains is either `P` or `X`

Diagram of an empty queue
```
         [ E ]
    [ E ]     [ E ] H
         [ X ]
           T
```

## Full condition

The queue is full when:
- from the point of the producer, the state right after the tail position contains a `D`
- from the point of the consumer, the state at the head position is not `E`

Diagram of a full queue without overflow
```
         [ D ]
    [ D ]     [ X ] H T
         [ D ]
```

## Overflow condition

An overflow occurred when:
- from the point of the producer, the state at the new tail positions contained a `D` when advancing
  the `X` state
- from the point of the consumer, the state at the head position is not `E` or does not contain an `X`
    - this condition is not sufficient to detect an overflow since the `X` could have been advanced
      a full wrap-around; this could be solved by an additional `O` flag at the expense of a CAS
      in the `push` operation; although one could potentially manage to do it without a loop and at
      max 3 consecutive CAS; if more CAS are required, the queue would be corrupted; this would
      again make the `push` operation wait-free

Diagram of a full queue with overflow
```
         [ D ]
  H [ D ]     [ X ] T
         [ D ]
```

The head position always points to a state containing an `E`, except there was an overflow, then it
has to find the `X` and continue to read state data thereafter.

The tail position always points to a state containing a `X` after a push is finished. In between it
can point to a `P` or `D`.

## Push operation

A push operation always writes an `X` to the new tail position. This must be an atomic `exchange` and can
be a `relaxed` access. If the returned state contains a `D`, the ownership on the corresponding
data is transferred back to the producer and it can later be returned to the user. Next, the data
to push will be copied to the data buffer and an atomic `store` operation with `release` semantics
is performed. A CAS is not required since only the producer is allowed to write to the state buffer
when the respective position contains an `X` and since the producer is the only one allowed to write the `X`
one does not have to take care of the actual value, even when the consumer accidentally overwrote it.
Depending on the use case one could do an `exchange` and inform the user about the corruption when
the state does not contain `X`. For more robustness and intermediate `P` state with additional
transaction actions could be used. This will be explained later on in order to not over complicate
the push operation even more.

Diagram of a simple push sequence
```
           H                      H                      H
         [ E ]                  [ E ]                  [ E ]
    [ E ]     [ X ] T  ->  [ E ]     [ X ] T  ->  [ E ]     [ D ]
         [ E ]                  [ X ]                  [ X ]
                                                         T
```

## Pop operation

A pop operation is valid when at the new head position a transition from `D` to `E` happens and when the
state at the current head position is an `E`, `X` or `XI`. This must be accomplished with a atomic load
operation with `acquire` semantics on the new head position. A value of either `D` or `DI` indicates that
data is available at the new position. The `acquire` semantic synchronizes the memory from the
data buffer. An additional speculative CAS with `relaxed` semantics needs to be performed at the old head
position. The expected value must be `E`. It is not important whether the CAS is successful, either the
value was already `E` which would not change anything or it fails. If it fails and the actual value
is a `X` or `XI`, the data at the new head position is still valid and a CAS with `E` as new state and with
the previously obtained state from the new head position as the expected state can be performed at the new
head position. This procedure is required to detect `ABA` problems due to an overflow.
When the CAS is successful the ownership is obtained and the locally cached head position needs to be set to
the new head position, if not successful, then an overflow occurred in the meantime.
If the state at the old head position was neither `E` nor `X` nor `XI`, then also an overflow occurred and
the consumer has to find `X` or `XI` to be able to take data out of the queue.

To distinguish the scenario from the catching up of the tail position with the head position, an `O` flag
could be added to the state when the push operation performed an overflow. Further down a robust overflow
detection is described in more detail. Further investigation needs to be done to determine whether it is
important to know if it just happens that the queue is full without overflow or whether there was an overflow
and in addition a full wrap-around of the tail position happened with the tail position being in front
of the head position again. The only meaningful information one could take out of this is
to have certainty an overflow occurred and inform the user about this situation. The cost of this feature
might be a downgrade from a wait-free `push` to a lock-free `push` since a CAS operation is required to advance
the `X` state, although there might be a solution for this problem as mentioned above.

In case the `pop` operation encounters an overflow, the consumer has to find the new end of the queue. This action is
performed by walking through the state buffer and reading the state at each position until the `X` is found or until a full wrap around
happens. It might happen that the producer added new data to the queue and that after one wrap around no `X` was found.
In this case the consumer would have to continue looking for the `X`. In case the queue is corrupt, this would result
in an infinite loop. To detect this scenario, the consumer adds an `I` to the previously read state and does a CAS
operation with the new and old state. If there is a wrap-around without finding the `X` and all states have a `I` flag,
the queue is corrupt and the `pop` operation can be aborted with an error. Consecutive `pop` operations would fail in
a similar way. It is the task of the user to decide what to do when such a scenario is detected. It can potentially
be fixed by a `push` operation but that might be dangerous.

To prevent starvation, due to a high frequency producer, the consumer could configure the number of wrap-around which
are allowed to be performed in order to find the new head position. If the consumer would not be able to find the
new head withing this amount of wrap-around the `push` operation could be aborted with an error code, indicating a
potential abnormal behaviour of the producer.

The `pop` operation is not allowed to overtake a `X` state except when the state at head position is not an `E` which
indicates that and overflow happened or is about to happen with the next `push` operation.

## Crash recovery

For crash recovery the operations performed to the state buffer with the atomics needs to recoverable and more important,
the ownership of the data must be unambiguous since it might be a handle to a resource. To accomplish this, transaction
objects need to be stored in a persistent memory. The procedure will be described for a `push` operation but a `pop` operation
follows the same principles. Before the `X` state is advanced, the transaction object is set to the `about-to-flag-next-state-with-P` state.
Additionally, the corresponding tail position is stored in the transaction object. Instead of advancing `X` immediately,
the state at the new tail position is read and flagged with `P` in a CAS operation. If a crash happens before the next state is
flagged with `P`, the restarted producer will notice that `P` is not set and try to continue the operation. If the crash
happens after `P` is set, the transaction object is updated to `about-to-advance-X` and in addition the old state, which
might include a `D` is also stored in the transaction object. If the crash happens before the state is set to `XP`, the
restarted producer will notice that `XP` is not set and try to continue the operation. If the crash happens after `XP` is set,
the transaction object still contains the information that an overflow happened due to the potential `D` flag in the cached state.
With this information the ownership can still be obtained and a leak prevented. The transaction object also has a flag to indicate
whether is is currently active or not. It is important to know that this mechanism only works when there is a recovery phase
during the restart of the producer and a crash during this recovery phase will break any further recovery attempt and a full restart
of the resource manager is required to ensure there are no leaks. The transaction process continues similar to above for writing
the actual data to the buffer and releasing the ownership to the queue. It is important to know that intermediate steps with the
`P` flag are required and the transaction object need to store all the required information in order the unambiguously detect at
which point the crash happens and how to restore the operation. The `pop` operation could be performed in a similar way but it
might be required to have an additional flag for a pending consumer operation, e.g. `C`. In this case it has to be determined what
happens when a state has `DPC` flags. The rules for the overflow recovery might also change slightly but overall it should be a
similar mechanism.

There might be some corner cases which are not yet described but they should be solved by means described above and by
not violating the invariant from above. At worst, an additional flag might be required.

## Memory layout and access rights

The head and tail positions do not need to be stored alongside the state buffer but can be in a local address space.
For increased robustness with transactional `push` and `pop` operations they should be stored in a persistent memory.
For increased performance producer and consumer could have read only access to the tail and head position.

The data buffer needs write access from the producer but can be read only from the consumer. In this case the data must
be trivially copyable. The only memory which needs to be mutably shared between producer and consumer are the atomics
from the state buffer. Since the states fit into 1 byte, this is quite cache efficient and it makes the queue lock-free
for 8 bit architectures. It has to be measured whether the false sharing of the states has a bigger performance impact than
the gains from the advantage of having as many states as possible on the same cache line. If the performance impact is notable,
one state item per cache line would fix the issue and the time-space-tradeoff would lean towards using more space.

## Robust overflow detection on consumer side

There is one situation on the consumer side which is indistinguishable from an situation with a full queue with a potential overflow
on the next push operation and a full wrap-around with overflowing push operations as show in the diagram below.

```
         [ D ]
    [ D ]     [ X ] H T
         [ D ]
```

A robust overflow detection on the consumer side is more complex than just using an additional `O` flag on each state with a `D` flag.

The problem is that if the queue is full and the producer pushes two times, there would be two consecutive positions
with an `O` flag but there wouldn't be a logical gap between these two positions
-> the `O` flag is added to `X` when the producer gets a `D` while advancing the `X` flag
-> when the consumer does not point to an `E` but a `X` without `O` it still has a chance to prevent an overflow if it can take
   the data from the position in the queue ahead of tail (the position ahead might already have the `XO` flags set instead of `D` when
   the consumer tries to pop during the overflowing push operation)
-> when the consumer does not point to an `E` but a `X` with a `O` flag, an overflow occurred
-> when the consumer does not point to an `E` nor a `X` an overflow occurred and the `X` needs to be found
-> when the `XO` is found, the consumer resets the `O` flag and tries to take the data ahead of the tail position
    -> if this is successful, all further `O` flags on positions with `D` states are ignored as long as no overflow is detected, i.e.
       the current head position points to a state with an `E` flag
    -> if the tail advanced in the meantime the operation is not successful and the new tail needs to be found

Diagram with robust overflow detection on consumer side.
```
         [ DO]
    [ DO]     [ XO] H T
         [ DO]
```

The robust detection of an overflow on the consumer side adds complexity to the queue and makes the transactional `push`/`pop`
operations even more complex. It has to be decided if a weak overflow detection on the consumer side is sufficient. The weak overflow
detection could be done by e.g. caching the the tail position on the consumer side and in case an ambiguous situation is detected,
the new tail position could indicate an overflow. This might be racy though since the writing of the state and tail position are
independent from each other. Some further thoughts must be given to the solution with the tail position.

Another solution would be to let the user solve the problem by e.g. using a sequence number in the data and a gap in the sequence number
would indicate an overflow. This has the additional advantage to be able detect how many data chunks are lost.

## Batch push

In theory it is also possible to send multiple data at the same time and ensure the consumer will only notice the new data once
the producer has written all the data into the queue. For this `push` operation, the producer will set the `P` flag to all the
positions which will hold new data. If there is already some data at those indices, i.e. the overflow case, a lambda provided
to the `push` method will be called in order to release a potential resource. The producer now writes all the data to the
data buffer and sets the `D` flags and therefore also removes the `P` flags in reverse order. Once the last `P` flag is reset
the data becomes available to the consumer.

## Multi producer extension

In theory it should not be too complicated to use the idea for this queue to create a lock-free multi producer queue. Some of
the more sophisticated features like crash recovery might not be possible anymore with a multi producer behaviour.

To make the queue multi producer capable, something like the `IndexQueue` from `iceoryx` could be used in order to get the memory
for the producer to store the data. The corresponding memory for the data chunk would then be stored in a struct alongside the
state flags. The atomic operations would then be performed on the struct with the combined flag and index data. It should be
possible to keep the struct size at or below 4 bytes (32 bit) and therefore maintaining lock-free behaviour on 32 bit platforms.

## io_uring like cancelation operations

If the queue is used to asynchronously distribute tasks, similar to the mechanism from`io_uring`, it might be handy to cancel
already enqueued tasks. With each `push` operation the producer could provide a handle to the user to cancel the operation.
The handle would correspond to the position where the task was enqueued and the producer could use this information to
write an `E` to that position with a CAS or even a plain `exchange` operation. The `ABA` problem needs to be considered by e.g.
using a generation counter alongside the position.

## Priority queue

It needs to be explored if the idea with the cancelation operation can be extended to create a fully fledged lock-free priority queue.

## TODO

More detailed diagrams showing each scenario will be created. This should make it more clear what happens
when specific states are present and how to perform state resolutions in order to come to a consistent state.

# License of this document

CC-BY-NC-SA 4.0
