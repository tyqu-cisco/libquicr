MLS in M10x Architecture
========================

Objective: Enable M10x clients to use MLS to configure keys for end-to-end
encryption, without requiring any infrastructure beyond the pubsub system.

## The Overall Vision

* The participants in a session exchange MLS messages to set up a key for the
  current participants in the session
* The MLS context produces a stream of keys for SFrame
    * First: Make key available for *decryption*
    * Once quorum has key: Start using key for *encryption*
* One MLS context per meeting (or session, or comparable noun)
    * MLS context defines who has access to the keys
    * Meeting is the level at which membership / access control is done

## Quick MLS Overview + Requirements + Design Approaches

MLS Overview:
* Unit of MLS functionality is a _group_
    * At any given time, a group represents a secret known only to its _members_
    * Membership can change over time
    * Each time mebership changes (batch of joins or leaves), the shared secret
      is changed to one known only be the _current_ members
    * Each period of time with stable membership/secret is an _epoch_
* First participant creates group unilaterally, generates first secret
* Once the group is created:
    * Participants send requests to join and leave
    * (You can't leave unilaterally, someone remaining has to remove you)
    * Someone in the group **commits** the requests => Commit, Welcome messages
    * Commit message is sent to other clients already in the group
    * Welcome message is sent to new joiners
    * Members use Welcome/Commit to initialize/update to new epoch

Critical invariants:
* Only one group is created per meeting
* Linear sequence of Commits - Each Commit has exactly one successor

Design Approaches:
1. Centralized: Use a server to assure the invariants
    * Server effectively holds a lock for the group
    * ... so that only one member commits at a time
2. Decentralized: Clients use a distributed consensus protocol
    * Clients vote on which Commit will be applied for an epoch
    * This approach has poor partition-tolerance:
        * Uniqueness cannot be assured
        * Linearity requirement will stop progress on one side

Right now, the code base attempts to do (2), but it's very complex and fragile.
We should probably refactor to (1).  We can move toward a decentralized system
by replacing the centralized server with decentralized version behind the same
interface.

## Notional Software Architecture

* MLSClient - Handles MLS logic
    * Inputs: Quicr client, namespace
    * Outputs: SFrame keys
* QSFrameContext - Handles media protect/unprotect
* An MLSClient instance is owned by something representing the session
  (`QController`?)
* Each publish/subscribe delegate owns a QSFrameContext
    * Delegate uses QSFrameContext uses `QSFrameContext::protect/unprotect`
    * MLSClient instance holds a weak reference to every QSFrameContext
    * When MLS generates new keys, MLSClient updates the QSFrameContext
    * This updates the keys used to protect/unprotect media

MLSClient <-- handler thread <-- queue; commit thread
- PubDelegate <-- libquicr threads
- SubDelegate --> queue

```
- JoinRequest --> onSubscribedObject --> queue.push(JoinRequest)  [transport thread]
- queue.pop() --> validate --> join_requests_to_commit [handler thread]
- (timer) --> Commit(join_requests_to_commit) --> publish(commit) [commit thread]
- Commit --> onSubscribedObject --> queue.push(Commit)  [transport thread]
- queue.pop() --> validate --> mls_session.handle(commit) => new keys [handler thread]
    - for (const auto& delegate : delegate) { delegate.install_key(key); }
```

## Current Naming Scheme

Assumptions:
* Each group has a 56-bit globally unique ID
* Each client hasa 32-bit ID unique within the group, for the whole lifetime of
  the group.

```
For sending a request to join:

       group_id       op    sender      kp_id
 -------------------- -- ----------- -----------
|XX|XX|XX|XX|XX|XX|XX|01|XX|XX|XX|XX|XX|XX|XX|XX|


For sending a Welcome to a joiner:

       group_id       op    sender      kp_id
 -------------------- -- ----------- -----------
|XX|XX|XX|XX|XX|XX|XX|02|XX|XX|XX|XX|XX|XX|XX|XX|


For sending a Commit:

       group_id       op    sender      epoch
 -------------------- -- ----------- -----------
|XX|XX|XX|XX|XX|XX|XX|03|XX|XX|XX|XX|XX|XX|XX|XX|


For sending a requesting to be removed:

       group_id       op    sender        0
 -------------------- -- ----------- -----------
|XX|XX|XX|XX|XX|XX|XX|04|XX|XX|XX|XX|XX|XX|XX|XX|


For sending a CommitVote:

       group_id       op    sender      epoch
 -------------------- -- ----------- -----------
|XX|XX|XX|XX|XX|XX|XX|05|XX|XX|XX|XX|XX|XX|XX|XX|
```

## Abstracting over Design Decisions

Design ambiguities / Things to wrap in interfaces:
* LockService - HTTP epoch server or decentralized
* DistributionService - MLS message distribution over pubsub or WS

Join flow:
```c++
auto create_lock = lock_service.create();
switch (create_lock.type) {
  case CreateResponse::created:
    // Send KeyPackage
    delivery_service.send(key_package);
    const auto welcome = delivery_service.await(welcome_for_me);
    // Initialize state from welcome
  
  case CreateResponse::ok:
    // Create group locally
    create_response.release();
    // Error if not OK?

  case CreateResponse::conflict:
    // Retry in a second
}
```

Commit flow:
```c++
// Prepare commit

auto commit_lock = lock_service.commit();
switch (commit_lock.type) {
  case CommitLock::ok:
    delivery_service.send(commit);
    commit_lock.release();
    // Error if not OK?

}
```


# === Obsolete below this line ===

## Quick glance at the code

* `async_queue.h` - A queue that can be accessed from multiple threads
* `mls_client.h` - The actual logic for how MLS is done over Quicr pubsub
* `mls_session.h` - A simplifying wrapper around the MLSpp API
* `namespace_config.h` - Namespaces to sub/pubIntent to for an MLS group
* `pub_delegate.h` - A publish delegate that just reports PublishIntentResponse
* `sub_delegate.h` - A subscribe delegate that just calls `MLSClient::handle()`

## Protocol Overview (Serverless)

```
A: Create group
   Sub /group/join/*        
   Sub /group/leave/*
   Sub /group/commit/*
   Pub /group/commit_vote/a/*
   Pub /group/leave/a/*
   Pub /group/commit/a/*
   Pub /group/welcome/a/*
   Pub /group/commit_vote/a/*

# Join
B: Sub /group/welcome/* # Also other group namespaces as above
B -> /group/join/b: KeyPackage
/group/join/* -> A: KeyPackage // A adds B to the group => Welcome, Commit
A -> /group/welcome/a/b: Welcome
/group/welcome/* -> B: Welcome // B uses Welcome to join the group
A -> /group/commit/a/1: Commit
/group/commit/a/1 -> A: Commit // A applies Commit, now in group with B

C: Sub /group/welcome/* # Also other group namespaces as above
C -> /group/join/b: KeyPackage
/group/join/* -> A: KeyPackage // A adds C to the group => Welcome, Commit
A -> /group/welcome/a/c: Welcome
/group/welcome/* -> C: Welcome // C uses Welcome to join the group
A -> /group/commit/a/2: Commit
/group/commit/* -> A, B: Commit // Need quorum in 2-member group
A -> /group/commit_vote/a/2: CommitVote(a)
B -> /group/commit_vote/b/2: CommitVote(a)
/group/commit_vote/* -> A, B: CommitVote(a) x 2 // Quorum => A, B apply Commit, now with C

# Leave
C -> /group/leave/c: LeaveRequest
/group/leave/* -> A: KeyPackage // A removes C from the group => Commit
A -> /group/commit/a/3: Commit
/group/commit/* -> A, B: Commit // Need quorum in 2-member group
A -> /group/commit_vote/a/2: CommitVote(a)
B -> /group/commit_vote/b/2: CommitVote(a)
C -> /group/commit_vote/c/2: CommitVote(a)
/group/commit_vote/* -> A, B: CommitVote(a) x 2 // Quorum => A, B apply Commit, now with C
```


