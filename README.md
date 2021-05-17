`boundedbuffer.h` - thread-safe bounded buffer

`cacheFns.h` - functions used for determining victim files

`clientApi.h` - given API for the client

`fileparser.h` - key: value file parser

`filesystemApi.h` - core of the in-memory file storage system (read, write, insert, delete, lock/unlock operations)

`icl_hash.h` - hash table implementation from Keith Seymour's proxy library code (full copyright notice in `icl_hash.c`) 

`log.h` - logging system

`requestCode.h` - macros defining client request codes

`responseCode.h` - macros defining server status response codes

`scerrhand.h` - macros for handling errors from system calls

`misc.h` - miscellaneous utility functions and macros

`clientServerProtocol.h` - macros related to the communication protocol between clients and the server

`clientInternals.h` - functions shared both by the client API primitives and the higher-level functionalities in `client.c`
