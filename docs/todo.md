# Todo

- [] Implement smoothsort or heapsort for sorting the nearly sorted heap array of multi-shard candidates.
- [] Parallelize multi-shard inserts and searches.
- [] Unittest FaissIndexServiceImpl and ShardedIndexServiceImpl
- [] Add better CLI flag support in main entrypoints
- [] Persistence (See ## Persistence)
- [] Better abstractions (See ## Abstractions)

## Persistence

Currently, we will lose all data in a shard if the shard crashes, since we
serve data directly from memory. I would like to support some form of
persistence or replication to guard against this scenario.

Here are some alternatives:
- Persist indexes to disk (mmap files?) on every write to allow shards to
restart without data loss. I'm not sure mmap supports large files or exactly
how it works. Need to do more due diligence.
- Support failover shard replicas.
- Support a "snapshot" RPC which saves all data to disk.
- Insert vectors to a disk-backed WAL or queue (e.g. kafka, or my own).
We can "replay" vectors from last healthy timestamp to restore shards. With
upsert, this should get us back to the desired state eventually.

Resources:
- https://martinfowler.com/articles/patterns-of-distributed-systems/wal.html

## Abstractions

There only needs to be one index service impl.

The single-node index service calls directly to an index impl to
perform index ops.

There are two types of index impls:
* single-node (i.e. runs on this node)
* multi-node (i.e. calls out to other index services)

An in-memory index should support the following:
* upsert
* search
* describe
* (future) save (to disk)

We can then have different in-memory index impls:
* `faiss`
* `scann`
* a custom SIMD one I implement for fun

