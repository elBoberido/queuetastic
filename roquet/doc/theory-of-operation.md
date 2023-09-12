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

The queue consists of a buffer for the data and a buffer for states. Each index of the state
buffer corresponds to one index of the data buffer.

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

The state buffer has this initial states
```
        ------------------------
state: | E  | X  | E  | E  | E  |
        ------------------------
index: | 0  | 1  | 2  | 3  | 4  |
        ------------------------
```
The initial write index has the value 1 and the read index the value 0.

The queue is empty when:
- from the point of the producer, the state at the index before the write index contains an `E`
- from the point of the consumer, the state at the read index is `E` and the state thereafter
  contains is either `P` or `X`

The queue is full when:
- from the point of the producer, the state at the index after the write index contains a `D`
- from the point of the consumer, the state at the read index is not `E`

An overflow occurred when:
- from the point of the producer, the state at write index +1 contained a `D` when advancing the `X` state
- from the point of the consumer, the state at the read index is not `E` or does not contain an `X`
    - this condition is not sufficient to detect an overflow since the `X` could have been advanced
      a full wrap-around; this could be solved by an additional `O` flag at the expense of a CAS
      in the `push` operation; although one could potentially manage to do it without a loop and at
      max 3 consecutive CAS; if more CAS are required, the queue would be corrupted; this would
      again make the `push` operation lock-free

The read index always points to a state containing an `E`, except there was an overflow, then it
has to find the `X` and continue to read data thereafter.

The write index always points to a state containing a `X` after a push is finished. In between it
can point to a `P` or `D`.

A push operation always writes an `X` to write index +1. This must be an atomic `exchange` and can
be a `relaxed` access. If the returned state contains a `D`, the ownership on the corresponding
data is transferred back to the producer and it can later be returned to the user. Next, the data
to push will be copied to the data buffer and an atomic `store` operation with `release` semantics
is performed. A CAS is not required since only the producer is allowed to write to the state buffer
when the respective index contains an `X` and since the producer is the only one allowed to write the `X`
one does not have to take care of the actual value, even when the consumer accidentally overwrote it.
Depending on the use case one could do an `exchange` and inform the user about the corruption when
the state does not contain `X`. For more robustness and intermediate `P` state with additional
transaction actions could be used. This will be explained later on in order to not over complicate
the push operation even more.

A read operation is valid when at read index +1 a transition from `D` to `E` happens and when the
state at the current read index is an `E`, `X` or `XI`. This must be accomplished with a atomic load
operation with `acquire` semantics on read index +1. A value of either `D` or `DI` indicates that
data is available at that specific index. The `acquire` semantic synchronizes the memory from the
data buffer. An additional speculative CAS with `relaxed` semantics needs to be performed at read
index. The expected value must be `E`. It is not important whether the CAS is successful, either the
value was already `E` which would not change anything or it fails. If it fails and the actual value
is a `X` or `XI`, the data at read index +1 is still valid and a CAS with `E` as new state and with
the previously obtained state from read index +1 as the expected state can be performed at read index +1.
When the CAS is successful the ownership is obtained and the read index needs to be set to read index +1,
if not successful, then an overflow occurred in the meantime.
If the state at read index was neither `E` nor `X` nor `XI`, then also an overflow occurred and the consumer
has to find `X` or `XI` to be able to take data out of the queue.

To distinguish the scenario from the catching up of the write index with the read index, an `O` flag
could be added to the state when the push operation performed an overflow. Further investigation needs
to be done to determine whether it is important to know if it just happens that the queue is full without
overflow or whether there was an overflow and in addition a full wrap-around of the write index happened with
the write index being in front of the read buffer again. The only meaningful information one could take out of this is
to have certainty an overflow occurred and inform the user about this situation. The cost of this feature
would be a downgrade from a wait-free `push` to a lock-free `push` since a CAS operation is required to advance
the `X` state.

In case the `pop` operation encounters an overflow, the consumer has to find the new end of the queue. This action is
performed by walking through the state buffer and reading each index until the `X` is found or until a full wrap around
happens. It might happen that the producer added new data to the queue and that after one wrap around no `X` was found.
In this case the consumer would have to continue looking for the `X`. In case the queue is corrupt, this would result
in an infinite loop. To detect this scenario, the consumer adds an `I` to the previously read state and does a CAS
operation with the new and old state. If there is a wrap-around without finding the `X` and all states have a `I` flag,
the queue is corrupt and the `pop` operation can be aborted with an error. Consecutive `pop` operations would fail in
a similar way. It is the task of the user to decide what to do when such a scenario is detected. It can potentially
be fixed by a `push` operation but that might be dangerous.

The `pop` operation is not allowed to overtake a `X` state except when the state at read index is not an `E` which
indicates that and overflow happened or is about to happen with the next `push` operation.

For crash recovery the operations performed to the state buffer with the atomics needs to recoverable and more important,
the ownership of the data must be unambiguous since they might be handles to resources. To accomplish this, transaction
objects need to be stored in a persistent memory. The procedure is described for a `push` operation but is similar to
a `pop` operation. Before the `X` state is advanced, the transaction object is set to the `about-to-flag-next-state-with-P` state.
Additionally, the corresponding write index is stored in the transaction object. Instead of advancing `X` immediately,
the state at the new index is read and flagged with `P` in a CAS operation. If a crash happens before the next state is
flagged with `P`, the restarted producer will notice that `P` is not set and try to continue the operation. If the crash
happens after `P` is set, the transaction object is updated to `about-to-advance-X` and in addition the new state, which
might include a `D` is also stored in the transaction object. If the crash happens before the state is set to `XP`, the
restarted producer will notice that `XP` is not set and try to continue the operation. If the crash happens after `XP` is set,
the transaction object still contains the information that an overflow happened due to the potential `D` flag in the cached state.
With this information the ownership can still be obtained and a leak prevented. The transaction object also has a flag to indicate
whether is is currently active or not. It is important to know that this mechanism only works when there is a recovery phase
during the restart of the producer and a crash during this recovery phase will break any further recovery attempt and a full restart
of the resource manager is required to ensure there are not leaks. The transaction process continues similar to above for writing
the actual data to the buffer and releasing the ownership to the queue. It is important to know that intermediate steps with the
`P` flag are required and the transaction object need to store all the required information in order the unambiguously detect at
which point the crash happens and how to restore the operation. The `pop` operation could be performed in a similar way but it
might be required to have an additional flag for a pending consumer operation, e.g. `C`. In this case it has to be determined what
happens when a state has `DPC` flags. The rules for the overflow recovery might also change slightly but overall it should be a
similar mechanism.

There might be some corner cases which are not yet described but they should be solved by means described above and by
not violating the invariant from above. At worst, an additional flag might be required.

The read and write index do not need to be stored alongside the state buffer but can be in a local address space.
For increased robustness with transactional `push` and `pop` operations they should be stored in a persistent memory.
For increased performance producer and consumer could have read only access to the write and read index.

The data buffer needs write access from the producer but can be read only for the consumer. In this case the data must
be trivially copyable. The only memory which needs to be mutably shared between producer and consumer are the atomics
from the state buffer. Since the states fit into 1 byte, this is quite cache efficient and it makes the queue lock-free
for 8 bit architectures.

More detailed diagrams showing each scenario will be created. This should make it more clear what happens
when specific states are present and how to perform state resolutions in order to come to a consistent state.

In theory it is also possible to send multiple data at the same time and ensure the consumer will only notice the new data once
the producer has written all the data into the queue. For this `push` operation, the producer will set the `P` flag to all the places
which will hold the new data. If there is already some data at those indices, i.e. the overflow case, a lambda provided
to the `push` method will be called in order to release a potential resource. The producer now writes all the data to the
data buffer and sets the `D` flags and therefore also removes the `P` flags in reverse order.

# License of this document

CC-BY-NC-SA 4.0
