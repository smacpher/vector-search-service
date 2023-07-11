# Vector Search Service

A simple, performant vector search service written in C++ and using gRPC.

## Install

See [install.md](./docs/install.md).

## Design

First, we'll start with a single-node vector search service. [x]

Next, we'll add a multi-node vector search service that maps requests out to
multiple single-node services and reduces them (i.e. sorts union of results,
takes top-k). This will be started up with a static set of single-node search
services. It will fill up each single-node search service to capacity. [x]

Next, we'll add the ability to filter on simple metadata fields, starting with a flat map of
strings -> strings, and maybe eventually getting to full JSON search.

Next, we'll implement a way to dynamically provision new
single-node services to increase a multi-node service's capacity as needed.

Finally, we'll implement a write-ahead-log to ensure we don't lose vectors on
insert and to make it possible to replicate an index.

