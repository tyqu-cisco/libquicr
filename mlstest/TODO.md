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
* [X] Provide some facility for leaving / being removed
* [X] Encrypt MLS messages
* [X] Generalize the committer selection rule so that it's not just index==0
* [ ] Add coalescing for MLS proposals (?)
* [ ] Add a reordering queue for MLS commits
* [ ] Add a stub "epoch server"
* [ ] Refactor MLSSession::add/remove into one multi-Commit method
* [ ] Commit on join to populate the tree



Distributed consensus on Commits:
* Send Commit whenever you feel like it
* On receiving Commit:
    * Add commit to cache
    * If first for epoch N: Send Vote = PrivateMessage(EpochAuthenticator)
* On receiving Vote:
    * Add Vote to tally for corresponding Commit
    * If tally > quorum accept Commit, reset state



