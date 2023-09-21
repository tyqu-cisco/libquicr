TODO
====

* [X] Remove `sleep()` calls to the extent possible
* [X] Avoid raw pointers to the extent possible
* [X] Hide more logic inside MLSClient
    * Automatically subscribe to the required namespaces
    * Basically just expose join()
* [X] Fix namespace scheme
* [X] Handle commits so that multiple members can join
* [X] Unsubscribe from welcome channel after being welcomed
* [ ] Provide some facility for leaving / being removed
* [ ] Add a reordering queue for MLS commits
* [ ] Generalize the committer selection rule so that it's not just index==0
* [ ] Add a stub "epoch server"

