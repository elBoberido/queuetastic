- three transaction objects are stored in an array
- one transaction object contains the current pending transaction;
  the index to this object is stored in an atomic
- push and pop own each one of the remaining two transaction objects
- push and pop write their view of the world
  (source, expected next read counter position and in case of push also the expected overrun value)
  to their transaction object and do an atomic exchange with the pendig object
- after the exchange, they check if their view of the world was right and correct it if necessary

== terminology ========================================================================================================

                                                               ------
                                                              | push |
                                                              | r=0  | <- transaction pop
                                                              | v=A  |
     local copies of members are lower case -> r=2             ------
                  pop read counter (atomic) -> R=2
                                       -------------------     ------
                                      | F | B | C | D | E |   | pop  |
                            buffer ->  -------------------    | r=2  | <- transaction pending (atomic index)
                                      | 0 | 1 | 2 | 3 | 4 |   | v=#  |
                                       -------------------     ------
                      push read counter -> R=1
            push write counter (atomic) -> W=6                 ------
                                                    source -> | pop  |
                                              read counter -> | r=1  | <- transaction overrun
                                                     value -> | v=#  |
                                                               ------

== we have a BuRiTTO with some content ================================================================================
== push ===============================================================================================================

                                     right before    right after
                           initial     exchange        exchange
                            ------      ------          ------
                           | push |    | push |        | push |
                           | r=0  |    | r=0  |        | r=0  |
                           | v=A  |    | v=A  |        | v=A  |
                            ------      ------          ------
           R=2                                                                       R=2
   -------------------      ------      ------          ------               -------------------
  | F | B | C | D | E |    | pop  |    | pop  |        | push |             | F | G | C | D | E |
   -------------------     | r=2  |    | r=2  |-     ->| r=2  |              -------------------
  | 0 | 1 | 2 | 3 | 4 |    | v=#  |    | v=#  | \   /  | v=B  |             | 0 | 1 | 2 | 3 | 4 |
   -------------------      ------      ------   \ /    ------               -------------------
       R=1                                        X                                  R=2
       W=6                  ------      ------   / \    ------                       W=7
                           | pop  |    | push | /   \  | pop  |
       TODO                | r=1  |    | r=2  |-     ->| r=2  |             source of pending transaction was pop
       actual check is     | v=#  |    | v=B  |        | v=#  |             -> no overrun
       source is not push   ------      ------          ------
       and counter sync is done when counter from transaction is larger than local copy
== push ===============================================================================================================

                                      right before    right after
                           initial     exchange        exchange
                            ------      ------          ------
                           | push |    | push |        | push |
                           | r=0  |    | r=0  |        | r=0  |
                           | v=A  |    | v=A  |        | v=A  |
                            ------      ------          ------
           R=2                                                                       R=2
   -------------------      ------      ------          ------               -------------------
  | F | G | C | D | E |    | push |    | push |        | push |             | F | G | H | D | E |
   -------------------     | r=2  |    | r=2  |-     ->| r=3  |              -------------------
  | 0 | 1 | 2 | 3 | 4 |    | v=B  |    | v=B  | \   /  | v=C  |             | 0 | 1 | 2 | 3 | 4 |
   -------------------      ------      ------   \ /    ------               -------------------
           R=2                                    X                                      R=3
           W=7              ------      ------   / \    ------                           W=8
                           | pop  |    | push | /   \  | push |
                           | r=2 -|-   | r=3  |-     ->| r=2 -|-> n = 2     source of pending transaction was push;
                           | v=#  | \  | v=C  |        | v=B  |             but new counter in transaction object is
                            ------   \  ------          ------    n > o?    not larger than than the old one
                                      \                                     -> no overrun
                                       -------------------------> o = 2

== scenario 1: push ===================================================================================================

                                      right before    right after
                           initial     exchange        exchange
                            ------      ------          ------
                           | push |    | push |        | push |
                           | r=0  |    | r=0  |        | r=0  |
                           | v=A  |    | v=A  |        | v=A  |
                            ------      ------          ------
           R=2                                                                       R=2
   -------------------      ------      ------          ------               -------------------
  | F | G | H | D | E |    | push |    | push |        | push |             | F | G | H | I | E |
   -------------------     | r=3  |    | r=3  |-     ->| r=4  |              -------------------
  | 0 | 1 | 2 | 3 | 4 |    | v=C  |    | v=C  | \   /  | v=D  |             | 0 | 1 | 2 | 3 | 4 |
   -------------------      ------      ------   \ /    ------               -------------------
               R=3                                X                                          R=4
               W=8          ------      ------   / \    ------                               W=9
                           | push |    | push | /   \  | push |
                           | r=2 -|-   | r=4  |-     ->| r=3 -|-> n = 3     source of pending transaction was push;
                           | v=B  | \  | v=D  |        | v=C  |             new counter in transaction object is also
                            ------   \  ------          ------    n > o?    larger than than the old one
                                      \                                     -> overrun; return C
                                       -------------------------> o = 2

== scenario 2: pop ====================================================================================================

                                      right before    right after
                           initial     exchange        exchange

                   --------> r=2         r=3 \------------------\ r = 3     read counter from pending transaction is
                  /                      v=H /------------------/ v = H     equal to local read counter
                 /                                                          -> take values from transaction; return C
                /           ------      ------          ------    tr >= r?
               /           | push |    | pop  |        | push |
              /            | r=0  |    | r=3  |-     ->| r=3 -|-> tr = 3
             /             | v=A  |    | v=#  | \   /  | v=C -|-> tv = C
            /               ------      ------   \ /    ------
           R=2                                    X                                      R=3
   -------------------      ------      ------   / \    ------               -------------------
  | F | G | H | D | E |    | push |    | push | /   \  | pop  |             | F | G | H | D | E |
   -------------------     | r=3  |    | r=3  |-     ->| r=3  |              -------------------
  | 0 | 1 | 2 | 3 | 4 |    | v=C  |    | v=C  |        | v=#  |             | 0 | 1 | 2 | 3 | 4 |
   -------------------      ------      ------          ------               -------------------
               R=3                                                                       R=3
               W=8          ------      ------          ------                           W=8
                           | push |    | push |        | push |
                           | r=2  |    | r=2  |        | r=2  |
                           | v=B  |    | v=B  |        | v=B  |
                            ------      ------          ------



