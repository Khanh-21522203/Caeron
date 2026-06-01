# Caeron Roadmap

Complete step-by-step plan to port the Aeron Media Driver from Java to C++23.

## Low-Latency Techniques Coverage

Every low-latency technique from the original Aeron is covered:

| Technique | Phase | Description |
|-----------|-------|-------------|
| **Lock-free MPSC ring buffer** (CAS on tail) | 2.1 | Zero-mutex client → driver commands |
| **Lock-free SPSC broadcast** (monotonic tail) | 2.2-2.3 | Zero-mutex driver → client responses |
| **Memory-mapped IPC** (shared CnC file) | 1.4, 5.1 | Zero-copy client ↔ driver communication |
| **Flyweight pattern** (zero serialization) | 0, 4 | Direct buffer reads/writes, no encode/decode |
| **Tri-buffer term rotation** (CAS swap) | 3 | 3 term partitions per log, rotate via CAS |
| **Cache-line padding** (false sharing avoidance) | 2.1, 2.8 | 64-byte alignment on hot fields |
| **CAS-based concurrency** (no mutexes) | 2 | All shared state updated via atomic CAS |
| **Acquire/release ordering** (not SeqLock) | 0 | `UnsafeBuffer` ordered accessors |
| **`sendmmsg()`/`recvmmsg()`** (batch syscalls) | 1.1a | Multi-packet I/O to amortize syscall cost |
| **Sender-side coalescing** | 9.2 | Batch multiple DATA frames per `sendmmsg()` |
| **Pre-touch log buffer pages** | 1.4 | `madvise(WILLNEED)` / `memset` to avoid page faults |
| **`SO_BUSY_POLL`** (kernel busy poll) | 1.1 | Reduce UDP receive latency |
| **Idle strategies** (spin/yield/backoff/sleep) | 14.4 | Configurable agent idle behavior |
| **Batch block processing** | 3.6 | `TermBlockScanner` processes contiguous blocks |
| **NAK-based retransmit** (not ACK-based) | 13 | Negative acknowledgement — no per-message ACK overhead |
| **Flow control** (unicast + multicast variants) | 11 | Backpressure without blocking |
| **Spy subscriptions** (zero-copy from pub log) | 15 | Read directly from publication's log buffer |
| **IPC publications** (skip network entirely) | 15 | Local-only pub/sub via shared memory |
| **Threading modes** (SHARED/DEDICATED/NETWORK) | 14.5 | Configurable agent-to-thread mapping |
| **Multicast support** | 6, 11 | IP multicast with group-tagged flow control |

---

## Status Legend

| Symbol | Meaning |
|--------|---------|
| ✅ | Done |
| 🔧 | In progress |
| ⬜ | Not started |

---

## Phase 0: Scaffold ✅

The foundational scaffold is complete. All libraries compile, all tests pass.

| Step | Description | Status |
|------|-------------|--------|
| 1 | Project skeleton: CMake, directory structure, compiler warnings | ✅ |
| 2 | Core types, byte order, bit utilities, memory helpers, error codes | ✅ |
| 3 | `UnsafeBuffer` — the fundamental memory access abstraction | ✅ |
| 4 | Protocol flyweights (Header, DataHeader, Setup, SM, NAK, RTT, Error, ResponseSetup) | ✅ |
| 5 | Command flyweights (18 CnC message types) | ✅ |
| 6 | Log buffer descriptor and operations (stubs) | ✅ |
| 7 | CnC file descriptor and mapped log buffers (stubs) | ✅ |
| 8 | Platform layer stubs (mmap, udp_socket, epoll, thread, clock) | ✅ |

---

## Phase 1: Platform Layer Completion ✅

Make the platform layer fully functional.

| Step | Description | Java Source | Status |
|------|-------------|-------------|--------|
| 1.1 | `UdpSocket`: bind, connect, send_to, receive_from, multicast join/leave, non-blocking, socket options (SO_RCVBUF, SO_SNDBUF, IP_MULTICAST_TTL, IP_MULTICAST_IF, SO_BUSY_POLL, SO_PREFER_BUSY_POLL) | `media/UdpChannelTransport.java` | ✅ |
| 1.1a | Batch I/O: `sendmmsg()` / `recvmmsg()` wrappers for multi-packet syscall batching (reduces syscall overhead) | — | ✅ |
| 1.2 | `EpollPoller`: add/modify/remove fd, poll with timeout, edge-triggered events | `media/UdpTransportPoller.java` | ✅ |
| 1.3 | `Thread`: CPU affinity, thread naming (`pthread_setname_np`), join/detach | `concurrent/AgentRunner.java` | ✅ |
| 1.4 | `MemoryMappedFile`: create_new (truncate+ftruncate), map_existing, resize, page-aligned, pre-touch pages (`madvise(MADV_WILLNEED)` or `memset` to avoid page faults on hot path) | `buffer/MappedRawLog.java` | ✅ |
| 1.5 | `Clock`: `nano_time()` (CLOCK_MONOTONIC), `epoch_time()` (CLOCK_REALTIME) | `concurrent/EpochClock.java`, `NanoClock.java` | ✅ |
| 1.6 | Platform tests for all of the above | — | ✅ |

---

## Phase 2: Concurrent Structures Completion

Make all lock-free structures production-ready.

| Step | Description | Java Source | Status |
|------|-------------|-------------|--------|
| 2.1 | `ManyToOneRingBuffer`: CAS-based tail, validated read/write, capacity checks, message type filtering | `concurrent/ManyToOneRingBuffer.java` | ✅ |
| 2.2 | `BroadcastTransmitter`: monotonic tail, message writing, overflow handling | `concurrent/BroadcastTransmitter.java` | ✅ |
| 2.3 | `BroadcastReceiver`: cached tail, message scanning, lapped-buffer detection | `concurrent/BroadcastReceiver.java` | ✅ |
| 2.4 | `CountersManager`: allocate/free counters, 64-byte metadata slots (type_id, key, label), counter registration | `concurrent/CountersManager.java` | ✅ |
| 2.5 | `AtomicCounter`: get, set, increment, decrement, CAS, close | `concurrent/AtomicCounter.java` | ✅ |
| 2.6 | `ManyToOneConcurrentLinkedQueue`: enqueue/dequeue, sentinel node, ABA prevention | `concurrent/ManyToOneConcurrentLinkedQueue.java` | ✅ |
| 2.7 | `OneToOneConcurrentArrayQueue`: SPSC enqueue/dequeue, capacity validation | `concurrent/OneToOneConcurrentArrayQueue.java` | ✅ |
| 2.8 | `AtomicArrayQueue` / `QueuedPad`: cache-line padded wrappers | `concurrent/AtomicArrayQueue.java` | ✅ |
| 2.9 | `CachedEpochClock`, `CachedNanoClock`: time caching for duty cycle | `concurrent/CachedEpochClock.java` | ✅ |
| 2.10 | Concurrent structure tests (multi-threaded stress tests) | — | ✅ |

---

## Phase 3: Log Buffer Operations

Implement the term buffer manipulation functions.

| Step | Description | Java Source | Status |
|------|-------------|-------------|--------|
| 3.1 | `TermReader`: read frames from a term buffer, handle padding frames | `logbuffer/TermReader.java` | ✅ |
| 3.2 | `TermScanner`: scan for available data, return high-water-mark offset | `logbuffer/TermScanner.java` | ✅ |
| 3.3 | `TermRebuilder`: insert received frames into the correct term offset | `logbuffer/TermRebuilder.java` | ✅ |
| 3.4 | `TermGapScanner`: detect gaps in received data for NAK generation | `logbuffer/TermGapScanner.java` | ✅ |
| 3.5 | `TermGapFiller`: fill gaps with padding frames | `logbuffer/TermGapFiller.java` | ✅ |
| 3.6 | `TermBlockScanner`: scan for contiguous blocks (batch processing) | `logbuffer/TermBlockScanner.java` | ✅ |
| 3.7 | `TermUnblocker`: unblock a stuck publisher at a specific offset | `logbuffer/TermUnblocker.java` | ✅ |
| 3.8 | `LogBufferUnblocker`: detect and unblock stuck publishers across all terms | `logbuffer/LogBufferUnblocker.java` | ✅ |
| 3.9 | `BufferClaim`: claim space in term buffer for writing | `logbuffer/BufferClaim.java` | ✅ |
| 3.10 | `BlockHandler` / `RawBlockHandler`: callback interfaces for block scanning | `logbuffer/BlockHandler.java`, `RawBlockHandler.java` | ⬜ |
| 3.11 | `FragmentHandler` / `ControlledFragmentHandler`: callback interfaces for frame reading | `logbuffer/FragmentHandler.java`, `ControlledFragmentHandler.java` | ⬜ |
| 3.12 | Log buffer operation tests | — | ✅ |

---

## Phase 4: Protocol Completion

Add the missing flyweight and wire protocol details.

| Step | Description | Java Source | Status |
|------|-------------|-------------|--------|
| 4.1 | `ResolutionEntryFlyweight`: name resolution protocol frame | `protocol/ResolutionEntryFlyweight.java` | ✅ |
| 4.2 | Frame flag validation: verify BEGIN/END/EOS/REVOKED flag semantics | `protocol/DataHeaderFlyweight.java` | ✅ |
| 4.3 | `HeaderWriter`: finalize write_default_header with full field initialization | `logbuffer/HeaderWriter.java` | ✅ |
| 4.4 | Protocol tests for new flyweights | — | ✅ |

---

## Phase 5: CnC Client Communication

Implement the client-driver IPC protocol.

| Step | Description | Java Source | Status |
|------|-------------|-------------|--------|
| 5.1 | `CncFileDescriptor`: full implementation (meta data read/write, section offset calculation) | `CncFileDescriptor.java` | ✅ |
| 5.2 | `MappedLogBuffersFactory`: create/map log buffers from file paths | `MappedLogBuffersFactory.java` | ✅ |
| 5.3 | `DriverProxy`: write commands to the to-driver ring buffer (add_publication, add_subscription, remove, etc.) | `DriverProxy.java` | ✅ |
| 5.4 | `ClientCommandAdapter`: read commands from ring buffer and dispatch to DriverConductor | `ClientCommandAdapter.java` | ✅ |
| 5.5 | `ClientProxy`: write responses to the to-clients broadcast buffer | `ClientProxy.java` | ✅ |
| 5.6 | `DriverEventsAdapter`: read responses from broadcast buffer | `DriverEventsAdapter.java` | ✅ |
| 5.7 | CnC integration tests | — | ✅ |

---

## Phase 6: Network Media Layer

The transport abstraction layer above raw sockets. **22 Java classes.**

| Step | Description | Java Source | Status |
|------|-------------|-------------|--------|
| 6.1 | `UdpChannel`: parse channel URI (`aeron:udp?endpoint=host:port`), resolve addresses, multicast/interface config | `media/UdpChannel.java` | ⬜ |
| 6.2 | `UdpChannelTransport`: base transport with send/receive buffers, socket lifecycle | `media/UdpChannelTransport.java` | ⬜ |
| 6.3 | `SendChannelEndpoint`: outbound transport — socket, destinations, send logic | `media/SendChannelEndpoint.java` | ⬜ |
| 6.4 | `ReceiveChannelEndpoint`: inbound transport — socket, sources, receive dispatch | `media/ReceiveChannelEndpoint.java` | ⬜ |
| 6.5 | `DataTransportPoller`: poll for incoming DATA/SETUP frames, dispatch to Receiver | `media/DataTransportPoller.java` | ⬜ |
| 6.6 | `ControlTransportPoller`: poll for incoming SM/NAK/RTT frames, dispatch to Sender | `media/ControlTransportPoller.java` | ⬜ |
| 6.7 | `UdpTransportPoller`: low-level epoll-based UDP poller | `media/UdpTransportPoller.java` | ⬜ |
| 6.8 | `ReceiveDestinationTransport`: multicast destination management | `media/ReceiveDestinationTransport.java` | ⬜ |
| 6.9 | `MultiRcvDestination`: multi-destination receive (MDC) | `media/MultiRcvDestination.java` | ⬜ |
| 6.10 | `ImageConnection`: per-source connection tracking for received images | `media/ImageConnection.java` | ⬜ |
| 6.11 | `SocketAddressParser`: parse `host:port` strings, IPv4/IPv6 support | `media/SocketAddressParser.java` | ⬜ |
| 6.12 | `NetworkUtil`: interface enumeration, multicast interface selection | `media/NetworkUtil.java` | ⬜ |
| 6.13 | `PortManager` / `WildcardPortManager`: dynamic port allocation for MDC | `media/PortManager.java`, `WildcardPortManager.java` | ⬜ |
| 6.14 | `InterfaceSearchAddress` / `ResolvedInterface` / `UnresolvedInterface` / `NamedInterface`: interface resolution | `media/*.java` | ⬜ |
| 6.15 | Media layer tests | — | ⬜ |

---

## Phase 7: Name Resolution

DNS and custom name resolution for channel endpoints.

| Step | Description | Java Source | Status |
|------|-------------|-------------|--------|
| 7.1 | `NameResolver` interface: resolve endpoint names to socket addresses | `NameResolver.java` | ⬜ |
| 7.2 | `DefaultNameResolver`: DNS-based resolution | `DefaultNameResolver.java` | ⬜ |
| 7.3 | `DriverNameResolver`: driver-level resolver with cache and re-resolution | `DriverNameResolver.java` | ⬜ |
| 7.4 | `DriverNameResolverCache`: TTL-based cache for resolved addresses | `DriverNameResolverCache.java` | ⬜ |
| 7.5 | `TimeTrackingNameResolver`: resolver with timing metrics | `TimeTrackingNameResolver.java` | ⬜ |
| 7.6 | `NameResolverAgent`: standalone agent for async name resolution | `NameResolverAgent.java` | ⬜ |
| 7.7 | Name resolution tests | — | ⬜ |

---

## Phase 8: Driver Conductor

The central agent that manages publications, subscriptions, images, and clients.

| Step | Description | Java Source | Status |
|------|-------------|-------------|--------|
| 8.1 | `DriverConductor` class: agent loop (`doWork()`), duty cycle tracking | `DriverConductor.java` | ⬜ |
| 8.2 | `ClientCommandAdapter`: process all 18 command types from CnC ring buffer | `ClientCommandAdapter.java` | ⬜ |
| 8.3 | `ClientProxy`: write responses (publication_ready, subscription_ready, error, etc.) | `ClientProxy.java` | ⬜ |
| 8.4 | `AeronClient`: client registration, heartbeat tracking, timeout detection | `AeronClient.java` | ⬜ |
| 8.5 | Publication management: `NetworkPublication`, `IpcPublication`, log buffer allocation | `NetworkPublication.java`, `IpcPublication.java` | ⬜ |
| 8.6 | Subscription management: `NetworkSubscriptionLink`, `IpcSubscriptionLink`, `SpySubscriptionLink` | `SubscriptionLink.java` and subclasses | ⬜ |
| 8.7 | `PublicationImage` creation: register images for subscriptions, position tracking | `PublicationImage.java` | ⬜ |
| 8.8 | `CounterLink`: link counters to their owning resources | `CounterLink.java` | ⬜ |
| 8.9 | `PublicationLink`: link publications to clients | `PublicationLink.java` | ⬜ |
| 8.10 | `PublicationParams` / `SubscriptionParams`: parse channel URI parameters | `PublicationParams.java`, `SubscriptionParams.java` | ⬜ |
| 8.11 | `SessionKey`: session deduplication for multicast | `SessionKey.java` | ⬜ |
| 8.12 | `DriverManagedResource` interface: common lifecycle for managed resources | `DriverManagedResource.java` | ⬜ |
| 8.13 | `UntetheredSubscription`: manage untethered subscription lifecycle (warm/hot/cold) | `UntetheredSubscription.java` | ⬜ |
| 8.14 | `PendingSetupMessageFromSource`: track pending SETUP messages for spy subscriptions | `PendingSetupMessageFromSource.java` | ⬜ |
| 8.15 | DriverConductor tests | — | ⬜ |

---

## Phase 9: Sender Agent

The agent that sends data frames over the network.

| Step | Description | Java Source | Status |
|------|-------------|-------------|--------|
| 9.1 | `Sender` class: agent loop, iterate publications, dispatch control frames | `Sender.java` | ⬜ |
| 9.2 | `NetworkPublication`: send logic — read from log buffer, construct DATA frames, advance position, coalesce multiple frames per `sendmmsg()` call to amortize syscall overhead | `NetworkPublication.java` | ⬜ |
| 9.3 | `NetworkPublicationThreadLocals`: per-thread scratch buffers (iovec arrays, message headers) for batch sending | `NetworkPublicationThreadLocals.java` | ⬜ |
| 9.4 | `RetransmitHandler`: buffer and resend lost frames on NAK | `RetransmitHandler.java` | ⬜ |
| 9.5 | `RetransmitSender`: actual retransmit send logic | `RetransmitSender.java` | ⬜ |
| 9.6 | `StaticDelayGenerator` / `OptimalMulticastDelayGenerator`: retransmit delay strategies | `FeedbackDelayGenerator.java` implementations | ⬜ |
| 9.7 | Status message processing: update flow control based on SMs | — | ⬜ |
| 9.8 | RTT measurement processing: handle echo timestamps | — | ⬜ |
| 9.9 | `SenderProxy`: thread-safe proxy for Conductor → Sender commands | `SenderProxy.java` | ⬜ |
| 9.10 | Sender tests | — | ⬜ |

---

## Phase 10: Receiver Agent

The agent that receives data frames from the network.

| Step | Description | Java Source | Status |
|------|-------------|-------------|--------|
| 10.1 | `Receiver` class: agent loop, manage channel endpoints | `Receiver.java` | ⬜ |
| 10.2 | `DataPacketDispatcher`: dispatch incoming DATA/SETUP frames to correct `PublicationImage` | `DataPacketDispatcher.java` | ⬜ |
| 10.3 | `PublicationImage`: receive logic — insert into term buffer via `TermRebuilder`, track high-water-mark | `PublicationImage.java` | ⬜ |
| 10.4 | `ImageConnection`: per-source state for received images | `media/ImageConnection.java` | ⬜ |
| 10.5 | SETUP frame handling: create new sessions, initialize term buffers, send SM | — | ⬜ |
| 10.6 | SM sending: periodic status messages with consumption progress, receiver window | — | ⬜ |
| 10.7 | NAK sending: gap detection (`TermGapScanner`) and NAK generation | — | ⬜ |
| 10.8 | RTT measurement: echo timestamp handling | — | ⬜ |
| 10.9 | `ReceiverProxy`: thread-safe proxy for Conductor → Receiver commands | `ReceiverProxy.java` | ⬜ |
| 10.10 | `ReceiverLivenessTracker`: track liveness of received image connections | `ReceiverLivenessTracker.java` | ⬜ |
| 10.11 | `DriverConductorProxy`: thread-safe proxy for Sender/Receiver → Conductor events | `DriverConductorProxy.java` | ⬜ |
| 10.12 | Receiver tests | — | ⬜ |

---

## Phase 11: Flow Control

All flow control strategies for multicast and unicast.

| Step | Description | Java Source | Status |
|------|-------------|-------------|--------|
| 11.1 | `FlowControl` interface: `onStatusMessage()`, `onSetup()`, `initialTermOffset()`, `limit()` | `FlowControl.java` | ⬜ |
| 11.2 | `FlowControlSupplier` interface: factory for flow control instances | `FlowControlSupplier.java` | ⬜ |
| 11.3 | `UnicastFlowControl`: simple unicast flow control | `UnicastFlowControl.java` | ⬜ |
| 11.4 | `MaxMulticastFlowControl`: multicast — track minimum receiver position | `MaxMulticastFlowControl.java` | ⬜ |
| 11.5 | `MinMulticastFlowControl` / `AbstractMinMulticastFlowControl`: multicast — track minimum with group tags | `MinMulticastFlowControl.java`, `AbstractMinMulticastFlowControl.java` | ⬜ |
| 11.6 | `TaggedMulticastFlowControl`: multicast with group tag filtering | `TaggedMulticastFlowControl.java` | ⬜ |
| 11.7 | `PreferredMulticastFlowControl`: preferred receiver with fallback | `PreferredMulticastFlowControl.java` | ⬜ |
| 11.8 | Supplier implementations: `DefaultMulticastFlowControlSupplier`, `DefaultUnicastFlowControlSupplier`, etc. | `Default*Supplier.java` | ⬜ |
| 11.9 | `FlowControlReceivers`: track receivers for flow control | `status/FlowControlReceivers.java` | ⬜ |
| 11.10 | Flow control tests | — | ⬜ |

---

## Phase 12: Congestion Control

| Step | Description | Java Source | Status |
|------|-------------|-------------|--------|
| 12.1 | `CongestionControl` interface: `onCongestion()`, `shouldMeasureRtt()`, `lastLoss()` | `CongestionControl.java` | ⬜ |
| 12.2 | `CongestionControlSupplier` interface | `CongestionControlSupplier.java` | ⬜ |
| 12.3 | `StaticWindowCongestionControl`: fixed window size (default) | `StaticWindowCongestionControl.java` | ⬜ |
| 12.4 | `DefaultCongestionControlSupplier` | `DefaultCongestionControlSupplier.java` | ⬜ |
| 12.5 | Congestion control tests | — | ⬜ |

---

## Phase 13: Loss Detection and Retransmit

| Step | Description | Java Source | Status |
|------|-------------|-------------|--------|
| 13.1 | `LossDetector`: timer-based gap re-NAK, expiry tracking | `LossDetector.java` | ⬜ |
| 13.2 | `LossHandler`: handle loss events from gap scanner, trigger retransmit requests | `LossHandler.java` | ⬜ |
| 13.3 | `FeedbackDelayGenerator` implementations: `StaticDelayGenerator`, `OptimalMulticastDelayGenerator` | `FeedbackDelayGenerator.java` | ⬜ |
| 13.4 | Loss detection tests | — | ⬜ |

---

## Phase 14: Threading and Agent Loops

| Step | Description | Java Source | Status |
|------|-------------|-------------|--------|
| 14.1 | `Agent` interface: `doWork()`, `onClose()`, `roleName()` | `concurrent/Agent.java` | ⬜ |
| 14.2 | `AgentRunner`: thread wrapper that runs an Agent's `doWork()` loop with idle strategy | `concurrent/AgentRunner.java` | ⬜ |
| 14.3 | `CompositeAgent` / `NamedCompositeAgent`: run multiple agents on one thread | `concurrent/CompositeAgent.java`, `NamedCompositeAgent.java` | ⬜ |
| 14.4 | Idle strategies: `BackoffIdleStrategy`, `NoOpIdleStrategy`, `SleepingIdleStrategy`, `YieldingIdleStrategy`, `SpinWaitIdleStrategy` | `concurrent/*IdleStrategy.java` | ⬜ |
| 14.5 | `ThreadingMode`: SHARED, DEDICATED, NETWORK (configure agent threading) | `ThreadingMode.java` | ⬜ |
| 14.6 | `MediaDriver`: top-level driver class — create context, start agents, manage lifecycle | `MediaDriver.java` | ⬜ |
| 14.7 | Shutdown coordination: `TerminationValidator`, graceful termination of all agents | `TerminationValidator.java`, `DefaultAllow/DenyTerminationValidator.java` | ⬜ |
| 14.8 | `DutyCycleTracker` / `DutyCycleStallTracker`: performance monitoring, stall detection | `DutyCycleTracker.java`, `status/DutyCycleStallTracker.java` | ⬜ |
| 14.9 | `AsyncExecutor` / `AsyncExecutorProxy`: async task execution for driver | `AsyncExecutor.java`, `AsyncExecutorProxy.java` | ⬜ |
| 14.10 | Threading tests | — | ⬜ |

---

## Phase 15: IPC and Spy Subscriptions

| Step | Description | Java Source | Status |
|------|-------------|-------------|--------|
| 15.1 | `IpcPublication`: local-only publication that skips the network | `IpcPublication.java` | ⬜ |
| 15.2 | `IpcSubscriptionLink`: link subscriptions to IPC publications | `IpcSubscriptionLink.java` | ⬜ |
| 15.3 | `SpySubscriptionLink`: read directly from a publication's log buffer | `SpySubscriptionLink.java` | ⬜ |
| 15.4 | `Subscribable` interface: common interface for images and IPC publications | `Subscribable.java` | ⬜ |
| 15.5 | `SubscriberPosition`: track subscriber position with position indicator | `SubscriberPosition.java` | ⬜ |
| 15.6 | `Position` / `UnsafeBufferPosition`: position tracking implementations | `concurrent/Position.java` | ⬜ |
| 15.7 | Image management: add/remove images for subscriptions | — | ⬜ |
| 15.8 | IPC/spy tests | — | ⬜ |

---

## Phase 16: Configuration

| Step | Description | Java Source | Status |
|------|-------------|-------------|--------|
| 16.1 | `Configuration`: all driver configuration constants and defaults | `Configuration.java` | ⬜ |
| 16.2 | `MediaDriver::Context`: builder for all driver configuration properties | `MediaDriver.java` | ⬜ |
| 16.3 | `CommonContext`: shared context between client and driver | `CommonContext.java` | ⬜ |
| 16.4 | Environment variable overrides for all config properties | `Configuration.java` | ⬜ |
| 16.5 | `ChannelUri` / `ChannelUriStringBuilder`: parse and build `aeron:udp?endpoint=...` URIs | `ChannelUri.java`, `ChannelUriStringBuilder.java` | ⬜ |
| 16.6 | Validation and defaults for all parameters | — | ⬜ |
| 16.7 | Configuration tests | — | ⬜ |

---

## Phase 17: Status Counters

| Step | Description | Java Source | Status |
|------|-------------|-------------|--------|
| 17.1 | `SystemCounterDescriptor`: all system counter type IDs and labels | `status/SystemCounterDescriptor.java` | ⬜ |
| 17.2 | `SystemCounters`: allocate and manage system counters | `status/SystemCounters.java` | ⬜ |
| 17.3 | Position counters: `PublisherPos`, `PublisherLimit`, `SenderPos`, `SenderLimit`, `SenderBpe`, `ReceiverPos`, `ReceiverHwm`, `SubscriberPos` | `status/*.java` | ⬜ |
| 17.4 | Channel status counters: `SendChannelStatus`, `ReceiveChannelStatus` | `status/SendChannelStatus.java`, `ReceiveChannelStatus.java` | ⬜ |
| 17.5 | Socket address counters: `SendLocalSocketAddress`, `ReceiveLocalSocketAddress` | `status/*LocalSocketAddress.java` | ⬜ |
| 17.6 | NAK counters: `SenderNaksReceived`, `ReceiverNaksSent` | `status/*Naks*.java` | ⬜ |
| 17.7 | `ClientHeartbeatTimestamp`: per-client heartbeat counter | `status/ClientHeartbeatTimestamp.java` | ⬜ |
| 17.8 | `FlowControlReceivers`: track receivers for flow control | `status/FlowControlReceivers.java` | ⬜ |
| 17.9 | `MdcDestinations`: MDC destination tracking | `status/MdcDestinations.java` | ⬜ |
| 17.10 | `PerImageIndicator`: per-image status indicator | `status/PerImageIndicator.java` | ⬜ |
| 17.11 | `StreamCounter`: base class for stream-specific counters | `status/StreamCounter.java` | ⬜ |
| 17.12 | `StatusUtil`: utility functions for status counter management | `status/StatusUtil.java` | ⬜ |
| 17.13 | Status counter tests | — | ⬜ |

---

## Phase 18: Client Library

The client-side library for applications to communicate with the driver. **~30 Java classes.**

| Step | Description | Java Source | Status |
|------|-------------|-------------|--------|
| 18.1 | `Aeron`: top-level client entry point — connect to driver, create publications/subscriptions | `Aeron.java` | ⬜ |
| 18.2 | `ClientConductor`: client-side agent — manage publications, subscriptions, images | `ClientConductor.java` | ⬜ |
| 18.3 | `Publication` / `ConcurrentPublication` / `ExclusivePublication`: publish messages | `Publication.java`, `ConcurrentPublication.java`, `ExclusivePublication.java` | ⬜ |
| 18.4 | `Subscription` / `Image`: subscribe and receive messages | `Subscription.java`, `Image.java` | ⬜ |
| 18.5 | `Counter`: client-side counter access | `Counter.java` | ⬜ |
| 18.6 | `BufferBuilder`: resizable buffer for fragment assembly | `BufferBuilder.java` | ⬜ |
| 18.7 | `FragmentAssembler` / `ControlledFragmentAssembler` / `ImageFragmentAssembler`: fragment reassembly | `FragmentAssembler.java` etc. | ⬜ |
| 18.8 | `DriverProxy`: client → driver command proxy | `DriverProxy.java` | ⬜ |
| 18.9 | `DriverEventsAdapter`: read driver responses from broadcast buffer | `DriverEventsAdapter.java` | ⬜ |
| 18.10 | `LogBuffers` / `LogBuffersFactory` / `MappedLogBuffersFactory`: client-side log buffer access | `LogBuffers.java` etc. | ⬜ |
| 18.11 | Callback handlers: `AvailableImageHandler`, `UnavailableImageHandler`, `AvailableCounterHandler`, `UnavailableCounterHandler`, `PublicationErrorFrameHandler` | `*Handler.java` | ⬜ |
| 18.12 | `CommonContext`: shared client/driver context | `CommonContext.java` | ⬜ |
| 18.13 | `AeronCounters`: counter type definitions and helpers | `AeronCounters.java` | ⬜ |
| 18.14 | `ReadableCounter`: read-only counter access | `status/ReadableCounter.java` | ⬜ |
| 18.15 | `DirectBufferVector`: scatter/gather buffer vectors | `DirectBufferVector.java` | ⬜ |
| 18.16 | `ReservedValueSupplier`: callback for setting reserved values in frames | `ReservedValueSupplier.java` | ⬜ |
| 18.17 | Client library tests | — | ⬜ |

---

## Phase 19: Exceptions and Error Handling

| Step | Description | Java Source | Status |
|------|-------------|-------------|--------|
| 19.1 | Driver exceptions: `ConductorServiceTimeoutException`, `DriverTimeoutException`, etc. | `driver/exceptions/` | ⬜ |
| 19.2 | Client exceptions: `ConductorServiceTimeoutException`, `DriverTimeoutException`, `RegistrationException`, etc. | `client/exceptions/` | ⬜ |
| 19.3 | `ErrorCode` enum: all error codes used in CnC error responses | `ErrorCode.java` | ⬜ |
| 19.4 | Error handling tests | — | ⬜ |

---

## Phase 20: Integration and End-to-End Testing

| Step | Description | Java Source | Status |
|------|-------------|-------------|--------|
| 20.1 | Single-driver smoke test: start driver, add pub/sub, send/receive | — | ⬜ |
| 20.2 | Multi-client test: multiple clients communicating with one driver | — | ⬜ |
| 20.3 | IPC publication test: local-only pub/sub without network | — | ⬜ |
| 20.4 | Spy subscription test: spy reads from publication log buffer | — | ⬜ |
| 20.5 | Multicast test: multicast pub/sub with flow control | — | ⬜ |
| 20.6 | Loss and retransmit test: inject loss, verify NAK/retransmit recovery | — | ⬜ |
| 20.7 | Reconnect test: client disconnect/reconnect handling | — | ⬜ |
| 20.8 | Stress test: high-throughput, many concurrent streams | — | ⬜ |
| 20.9 | Binary compatibility test: interop with Java Aeron client/driver | — | ⬜ |
| 20.10 | Threading mode test: SHARED vs DEDICATED vs NETWORK modes | — | ⬜ |

---

## Reference: Java Aeron Source Mapping

| Phase | Java Source Path | Class Count |
|-------|-----------------|-------------|
| 0 | (scaffold) | — |
| 1 | `aeron-driver/.../media/UdpChannelTransport.java`, `aeron-client/.../concurrent/` | ~8 |
| 2 | `aeron-client/.../concurrent/` | ~12 |
| 3 | `aeron-client/.../logbuffer/` | ~12 |
| 4 | `aeron-client/.../protocol/` | ~10 |
| 5 | `aeron-client/.../CncFileDescriptor.java`, `aeron-client/.../DriverProxy.java` | ~8 |
| 6 | `aeron-driver/.../media/` | ~22 |
| 7 | `aeron-driver/.../NameResolver.java` and subclasses | ~6 |
| 8 | `aeron-driver/.../DriverConductor.java` and related | ~20 |
| 9 | `aeron-driver/.../Sender.java` and related | ~10 |
| 10 | `aeron-driver/.../Receiver.java` and related | ~12 |
| 11 | `aeron-driver/.../FlowControl.java` and subclasses | ~10 |
| 12 | `aeron-driver/.../CongestionControl.java` and subclasses | ~4 |
| 13 | `aeron-driver/.../LossDetector.java`, `LossHandler.java` | ~4 |
| 14 | `aeron-driver/.../MediaDriver.java`, `ThreadingMode.java`, `AgentRunner.java` | ~10 |
| 15 | `aeron-driver/.../IpcPublication.java`, `SpySubscriptionLink.java` | ~6 |
| 16 | `aeron-driver/.../Configuration.java`, `ChannelUri.java` | ~6 |
| 17 | `aeron-driver/.../status/` | ~24 |
| 18 | `aeron-client/.../aeron/` (top-level) | ~30 |
| 19 | `aeron-driver/.../exceptions/`, `aeron-client/.../exceptions/` | ~10 |
| 20 | `aeron-system-tests/` | — |
| **Total** | | **~193** |
