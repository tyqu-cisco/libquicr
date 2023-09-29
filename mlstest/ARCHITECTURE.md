MLS in M10x Architecture
========================

Objective: Enable M10x clients to use MLS to configure keys for end-to-end
encryption, without requiring any infrastructure beyond the pubsub system.

## Quick glance at the code

* `async_queue.h` - A queue that can be accessed from multiple threads
* `mls_client.h` - The actual logic for how MLS is done over Quicr pubsub
* `mls_session.h` - A simplifying wrapper around the MLSpp API
* `namespace_config.h` - Namespaces to sub/pubIntent to for an MLS group
* `pub_delegate.h` - A publish delegate that just reports PublishIntentResponse
* `sub_delegate.h` - A subscribe delegate that just calls `MLSClient::handle()`

## Protocol Overview

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
