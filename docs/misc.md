# Vector Search Service

A simple, performant vector search service written in C++, powered by gRPC and
FAISS.

## Install

See [install.md](./docs/install.md).

## Architecture

This project currently supports serving vector indexes on a single node or
sharded across multiple nodes.

Both the single-node and multi-node index share the same API interface, defined
by [index_service.proto](src/proto/index_service.proto).

### Single-node

A single-node index service serve a single index directly from memory. Client
requests to add vectors to an index, search an index, or describe an index are
serviced by the in-memory index.

```text
  ┌────────┐   ┌─────────────┐
  │ client ├──►│    index    │
  └────────┘   └─────────────┘
```

### Multi-node

A multi-node index service serves an index that is sharded across one or more
single-node index services. When a client makes a request, a multi-node index
service calls out to one or more single-node indexes and reduces the results.

```text
                                    ┌─────────────┐
                                ┌──►│index shard 0│
                                │   └─────────────┘
                                │
                                │   ┌─────────────┐
                                ├──►│index shard 1│
                                │   └─────────────┘
  ┌────────┐   ┌─────────────┐  │
  │ client ├──►│sharded index├──┤
  └────────┘   └─────────────┘  │        ...
                                │
                                │
                                │   ┌─────────────┐
                                └──►│index shard N│
                                    └─────────────┘
```

Specifically, the sharded index service does the following for each RPC:

* `Describe`: invokes `Describe` on each single-node index and sums
the total number of vectors
* `Upsert`: invoke an `Upsert` as necessary to each shard; for new vectors, it
greedily assigns them to the next shard(s) that have available capacity; for
existing vectors, it identifies which shard the vector is a part of and
updates it in that shard
* `Search`: invokes `Search` for each shard, returning the top-k candidates per
shard, and then takes the top-k from the union of all shard candidates

Currently, the capacity of each shard is fixed across all shards and is
specified at multi-node index startup time.

#### Greedy inserts

We greedily fill each index shard to capacity before moving on to the next. For
example, inserting __12k vectors across 3 shards, each with a capacity of 5k__
results in the following allocation:

```text
 5k vectors  5k vectors   2k vectors  (# of vectors)

      x           x
      x           x
      x           x
      x           x           x
      x           x           x
 ┌─────────┐ ┌─────────┐ ┌─────────┐
 │ shard 0 │ │ shard 1 │ │ shard 2 │
 └─────────┘ └─────────┘ └─────────┘

     100%        100%        40%      (% of capacity)
```

Why do we do this? It makes incrementally scaling up the capacity of an index
easier and resource efficient. For example, if our index hits its
capacity, we can incrementally add another shard to increase its capacity, and
continue doing this as needed. Even if we over estimate the required capacity
of our index and add too many shards when we create a new index, we could
scale back down empty shards until they are needed (in theory, since this
autoscaling behavior isn't implemented yet :P).

Consider an alternative approach of inserting vectors to shards in
round robin order. We could also incrementally add more shards when we hit
capacity but until we fill up all shards, we will have multiple partially
filled shards that we can't scale down.

As an optimization, we don't make requests to shards that we know are empty
for `Describe` and `Search` RPCs. As mentioned before, we go further and
scale these shards down to free up resources until they are actually needed.

#### Multi-node upsert

To support upserting vectors across shards, the multi-node index maintains a
mapping from vector IDs -> the shard they belong to. When a new vector is
inserted, a new entry is added to this map. When an existing vector is updated,
we check the map to see which shard to upsert the vector to.

We assume that `Upsert` requests can contain a mix of new and existing vectors.
We construct a batch of vectors for each shard that we must upsert to. This
is determined by the current capacity of the shards; as mentioned before,
we greedily fill each shard to capacity until moving on to the next. It's also
determined by what vectors already exist in some shards; it's possible we'll
see at least one existing vector for each shard, and therefore need to make
requests to all shards.

For existing vectors, we add them to the batch corresponding to the
shard they are in. For new vectors, we greedily assign them to batches
based on the current capacity of shards. Consider the following visual example:

```text
                                                          ┌─────────┐
                                                          │ shard 0 │
                                                          └─────────┘
                               batch 0
                                                        ┌──────────────┐
                           ┌──────────────┐          ┌─►│id: 1 (update)│
                           │id: 1         │          │  ├──────────────┤
                           ├──────────────┤ upsert 0 ├─►│id: 2 (update)│
                      ┌───►│id: 2         ├──────────┤  ├──────────────┤
 vectors to upsert    │    ├──────────────┤          └─►│id: 4 (update)│
                      │    │id: 4         │             ├──────────────┤
 ┌──────────────┐     │    └──────────────┘             │id: 7         │
 │id: 1         │     │                                 ├──────────────┤
 ├──────────────┤     │                                 │id: 8         │
 │id: 2         ├─────┘                                 └──────────────┘
 ├──────────────┤
 │id: 4         │
 ├──────────────┤                                         ┌─────────┐
 │id: 3         │              batch 1                    │ shard 1 │
 ├──────────────┤                                         └─────────┘
 │id: 6         ├─────┐    ┌──────────────┐
 ├──────────────┤     │    │id: 3         │             ┌──────────────┐
 │id: 5         │     │    ├──────────────┤ upsert 1 ┌─►│id: 3 (update)│
 └──────────────┘     └───►│id: 6         ├──────────┤  ├──────────────┤
                           ├──────────────┤          ├─►│id: 6 (update)│
                           │id: 5         │          │  ├──────────────┤
                           └──────────────┘          │  │id: 9         │
                                                     │  ├──────────────┤
                                                     └─►│id: 5 (add)   │
                                                        ├──────────────┤
                                                        │              │
                                                        └──────────────┘
```

Vectors 1, 2, and 4 already exist in shard 0 so we upsert them in a single
request to shard 0. Vectors 3 and 6 exist in shard 1, and vector 5 is new and
is greedily assigned to the next shard available, which is also shard 1, so
vectors 3, 6, and 5 are upserted in a single request to shard 1.

## Limitations

Currently the project doesn't support the following (but that may change!):
* indexes that require fitting (e.g. any index that clusters requires to prune
search space, like IVF)
* indexes other than those provided by FAISS
* indexes that don't support updating (i.e. removing and re-inserting) vectors
aren't supported

## Milestones

[x] Define API interfaces for describing an index, adding vectors to an index,
and searching an index.

[x] Implement a single-node index serving service powered by FAISS.

[x] Implement a multi-node index serving service that maps requests out to one
or more single-node services and reduces responses.

[ ] Implement index persistence so indexes are not lost during node restarts.

### Bonus milestones (for fun)

[ ] Implement a custom single-node index powered by SIMD.

[ ] Implement simple metadata index to support hybrid vector / keyword match
search.

[ ] Implement automatic scale-out for multi-node indexes that reach capacity.

[ ] Implement index (including multi-node indexes) replication.

[ ] Implement multi-tenant service for CRUD'ing indexes.

