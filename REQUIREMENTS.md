# Software requirements — data_recorder

These are the software requirements for the ROS 2 package `data_recorder`.
Each row is one behavior: the software shall do one thing, under a stated condition, in a way a test or an inspection can check.

A requirement states what the software does. The design and the source code are the implementation of that requirement.

| Field | Meaning |
| --- | --- |
| ID | Stable identifier. The number is never reused. |
| Requirement | `The software shall` + one function + the condition that makes it true or false. |
| Verification | `Test`: run the software and observe the result. `Inspection`: read the interface, the launch file, or the configuration. |
| Status | `Implemented`: the current package satisfies the requirement. |

Vehicle-level requirements (payload, speed, emergency stop) belong in a system requirements document. They are outside this package.

## Package

| ID | Requirement | Verification | Status |
| --- | --- | --- | --- |
| SW-PKG-001 | The ROS 2 package name shall be `data_recorder`. | Inspection | Implemented |
| SW-PKG-002 | The package shall provide the executable `data_recorder`. | Inspection | Implemented |
| SW-PKG-003 | The package shall provide the executable `data_recorder_rest`. | Inspection | Implemented |
| SW-PKG-004 | The recorder shall store recorded messages in MCAP files. | Test | Implemented |

## Launch

| ID | Requirement | Verification | Status |
| --- | --- | --- | --- |
| SW-LCH-001 | The package shall provide a launch file that starts one continuous recorder. | Inspection | Implemented |
| SW-LCH-002 | The package shall provide a launch file that starts one snapshot recorder. | Inspection | Implemented |
| SW-LCH-003 | The package shall provide a launch file that starts only the REST service. | Inspection | Implemented |
| SW-LCH-004 | A recorder started with node name `<name>` in namespace `system` shall provide `std_srvs/Trigger` services `/system/<name>/start_recording`, `/system/<name>/stop_recording`, and `/system/<name>/snapshot`. | Test | Implemented |
| SW-LCH-005 | The default continuous node name shall be `data_recorder` in namespace `system`. | Inspection | Implemented |
| SW-LCH-006 | The default snapshot node name shall be `data_recorder_snapshot` in namespace `system`. | Inspection | Implemented |
| SW-LCH-007 | Two recorders in the same namespace shall use different node names. | Test | Implemented |
| SW-LCH-008 | The diagnostics of each recorder shall also be available on the topic `/diagnostics`. | Test | Implemented |

## Continuous recording

| ID | Requirement | Verification | Status |
| --- | --- | --- | --- |
| SW-REC-001 | In continuous mode the recorder shall have two states: recording, and not recording. | Test | Implemented |
| SW-REC-002 | When recording starts, the recorder shall create one bag directory under the configured output directory. | Test | Implemented |
| SW-REC-003 | When `use_timestamp` is true, the bag directory name shall be `<prefix>_<YYYY_MM_DD-HH_MM_SS>`. | Test | Implemented |
| SW-REC-004 | When `use_timestamp` is false, the bag directory name shall be `<prefix>`. | Test | Implemented |
| SW-REC-005 | The recorder shall select the bag directory name when recording starts and shall keep that name until recording stops. | Test | Implemented |
| SW-REC-006 | When the selected bag directory already exists, the recorder shall select the next unused suffix `_2`, `_3`, and so on, and shall leave the existing directory unchanged. | Test | Implemented |
| SW-REC-007 | When the configured prefix is empty, the recorder shall use the prefix `rosbag2`. | Test | Implemented |
| SW-REC-008 | The recorder shall reject a prefix that contains `/` or `\`. | Test | Implemented |
| SW-REC-009 | The recorder shall reject a start request when the configured output directory does not exist. | Test | Implemented |
| SW-REC-010 | While recording is active, the recorder shall reject every parameter change. | Test | Implemented |
| SW-REC-011 | While recording is stopped, the recorder shall accept a new prefix, and the next start shall use that prefix. | Test | Implemented |
| SW-REC-012 | When `autostart_recording` is true, the recorder shall enter the recording state during node startup. | Test | Implemented |
| SW-REC-013 | A stop request while the recorder is not recording shall fail with the message `Not recording`. | Test | Implemented |
| SW-REC-014 | A snapshot request sent to a continuous recorder shall fail. | Test | Implemented |
| SW-REC-015 | When `record_all` is false, the recorder shall record only the topics in the configured topic list. | Test | Implemented |
| SW-REC-016 | When `record_all` is true, the recorder shall record every available topic. | Test | Implemented |
| SW-REC-017 | A topic entry `<name>:<number>` shall record the topic `<name>` and shall use `<number>` as the expected rate in hertz. | Test | Implemented |
| SW-REC-018 | Each topic in `transient_local_topics` shall be subscribed with transient-local durability and shall be written again into each new file opened by a split. | Test | Implemented |
| SW-REC-019 | When `split_size_mb` is greater than 0, the recorder shall open the next MCAP file in the same bag directory once the file reaches that size. | Test | Implemented |
| SW-REC-020 | When `split_duration_s` is greater than 0, the recorder shall open the next MCAP file in the same bag directory once the file reaches that duration. | Test | Implemented |
| SW-REC-021 | A value of 0 for `split_size_mb` or `split_duration_s` shall disable that split limit. | Test | Implemented |
| SW-REC-022 | When `min_free_disk_mb` is greater than 0 and the free space of the output directory falls below that limit, the recorder shall stop continuous recording. | Test | Implemented |
| SW-REC-023 | The recorder shall reject a start request when `min_free_disk_mb` is greater than 0 and the free space is already below that limit. | Test | Implemented |
| SW-REC-024 | A value of 0 for `min_free_disk_mb` shall disable the free-space stop. | Test | Implemented |
| SW-REC-025 | When compression is enabled and the MCAP options file exists, the recorder shall write MCAP files with the compression settings in that file. | Inspection | Implemented |
| SW-REC-026 | When compression is enabled and the MCAP options file is missing, the recorder shall continue recording without compression. | Test | Implemented |
| SW-REC-027 | On process shutdown during continuous recording, the recorder shall close the bag and write `topic_rates.csv` before the process exits. | Test | Implemented |

## Snapshot recording

| ID | Requirement | Verification | Status |
| --- | --- | --- | --- |
| SW-SNP-001 | In snapshot mode the recorder shall create one directory when the node starts, named `<prefix>_<YYYY_MM_DD-HH_MM_SS>` under the configured output directory. | Test | Implemented |
| SW-SNP-002 | The recorder shall keep that directory for the lifetime of the node. | Test | Implemented |
| SW-SNP-003 | Until a snapshot is stored, the recorder shall hold the recorded messages in memory. | Test | Implemented |
| SW-SNP-004 | The memory buffer shall retain at most `snapshot_cache_mb` megabytes of messages. | Test | Implemented |
| SW-SNP-005 | When the memory buffer is full, the recorder shall discard the oldest buffered messages. | Test | Implemented |
| SW-SNP-006 | The recorder shall arm the snapshot buffer during node startup when the mode is snapshot. | Test | Implemented |
| SW-SNP-007 | `snapshot_cache_mb` shall be greater than 0. | Test | Implemented |
| SW-SNP-008 | An accepted snapshot request shall store the buffer `snapshot_post_trigger_s` seconds after acceptance. | Test | Implemented |
| SW-SNP-009 | A `snapshot_post_trigger_s` value of 0 shall store the buffer during the snapshot request. | Test | Implemented |
| SW-SNP-010 | Messages received during the post-trigger interval shall be included in the stored snapshot. | Test | Implemented |
| SW-SNP-011 | Each stored snapshot shall be a separate MCAP file in the directory created at node start. | Test | Implemented |
| SW-SNP-012 | A snapshot request received while a post-trigger wait is already active shall fail with the message `A snapshot is already waiting to be stored`. | Test | Implemented |
| SW-SNP-013 | A rejected snapshot request shall leave the published last-trigger time unchanged. | Test | Implemented |
| SW-SNP-014 | After a snapshot is stored, the memory buffer shall contain only messages received after that store. | Test | Implemented |
| SW-SNP-015 | After each stored snapshot, the recorder shall write `topic_rates.csv` in the snapshot directory from the MCAP file just stored. | Test | Implemented |
| SW-SNP-016 | A stop request while the snapshot buffer is armed shall succeed and shall leave the buffer armed. | Test | Implemented |
| SW-SNP-017 | On process shutdown, the recorder shall discard messages that have not been stored, and shall leave stored MCAP files in the snapshot directory. | Test | Implemented |
| SW-SNP-018 | `split_size_mb`, `split_duration_s`, and `min_free_disk_mb` shall have no effect in snapshot mode. | Test | Implemented |
| SW-SNP-019 | After the snapshot node has started, the recorder shall reject every parameter change, including a change of prefix. | Test | Implemented |
| SW-SNP-020 | Each topic in `transient_local_topics` shall be present in each stored snapshot. | Test | Implemented |

## Topic rate check

| ID | Requirement | Verification | Status |
| --- | --- | --- | --- |
| SW-CHK-001 | On every stop of continuous recording, including a free-space stop and process shutdown, the recorder shall evaluate the recorded topic rates. | Test | Implemented |
| SW-CHK-002 | The rate check shall write `topic_rates.csv` in the bag directory. | Test | Implemented |
| SW-CHK-003 | `topic_rates.csv` shall contain the columns `topic`, `message_count`, `duration_sec`, `recorded_hz`, `expected_hz`, `tolerance_pct`, and `status`. | Inspection | Implemented |
| SW-CHK-004 | For a topic with an expected rate, `status` shall be `OK` when the recorded rate is within `tolerance_topic_rate` percent of the expected rate. | Test | Implemented |
| SW-CHK-005 | For a topic with an expected rate, `status` shall be `LOW` when the recorded rate is below the expected rate by more than the tolerance. | Test | Implemented |
| SW-CHK-006 | For a topic with an expected rate, `status` shall be `HIGH` when the recorded rate is above the expected rate by more than the tolerance. | Test | Implemented |
| SW-CHK-007 | For a topic without an expected rate, `status` shall be `N/A`. | Test | Implemented |
| SW-CHK-008 | When any checked topic is outside tolerance, the recorder shall keep the bag and shall report the stop as failed. | Test | Implemented |

## Diagnostics

| ID | Requirement | Verification | Status |
| --- | --- | --- | --- |
| SW-DIA-001 | Each recorder shall publish `~/diagnostics` as `diagnostic_msgs/DiagnosticArray` at `publish_state_rate` hertz. | Test | Implemented |
| SW-DIA-002 | Each recorder shall publish `~/recording_state` as `std_msgs/Bool` at `publish_state_rate` hertz. | Test | Implemented |
| SW-DIA-003 | `recording_state` shall be true while continuous recording is active. | Test | Implemented |
| SW-DIA-004 | `recording_state` shall be true while a snapshot buffer is armed. | Test | Implemented |
| SW-DIA-005 | `recording_state` shall be false while a continuous recorder is not recording. | Test | Implemented |
| SW-DIA-006 | Diagnostics shall include the fields `Recording`, `Mode`, `Directory`, `Information`, `FreeDiskMB`, and `RateCheck`. | Inspection | Implemented |
| SW-DIA-007 | While continuous recording is active, diagnostics shall include `RecordingStarted` in local time `YYYY-MM-DD HH:MM:SS`. | Test | Implemented |
| SW-DIA-008 | While continuous recording is active, diagnostics shall include `RecordingDuration` in the form `HH:MM:SS`. | Test | Implemented |
| SW-DIA-009 | After an accepted snapshot request, snapshot diagnostics shall include `LastTrigger` as that acceptance time in local time `YYYY-MM-DD HH:MM:SS`. | Test | Implemented |
| SW-DIA-010 | Snapshot diagnostics shall omit `MinFreeDiskMB`. | Inspection | Implemented |
| SW-DIA-011 | `FreeDiskMB` shall report the free space of the configured output directory, in megabytes. | Test | Implemented |

## REST interface

| ID | Requirement | Verification | Status |
| --- | --- | --- | --- |
| SW-API-001 | One REST process shall be able to control every recorder named in its configuration. | Test | Implemented |
| SW-API-002 | A recorder entry shall have the form `url_name:/absolute/node_name`. | Inspection | Implemented |
| SW-API-003 | The REST service shall listen on the configured bind address and port. | Test | Implemented |
| SW-API-004 | The REST service shall return HTTP 504 when a recorder service call exceeds `service_timeout_s`. | Test | Implemented |
| SW-API-005 | Every HTTP response shall include the header `Access-Control-Allow-Origin: *`. | Inspection | Implemented |
| SW-API-006 | `GET` and `POST /recording/<name>/start` shall start the named continuous recorder. | Test | Implemented |
| SW-API-007 | `GET` and `POST /recording/<name>/stop` shall stop the named continuous recorder. | Test | Implemented |
| SW-API-008 | `GET` and `POST /recording/<name>/trigger` shall request a snapshot from the named snapshot recorder. | Test | Implemented |
| SW-API-009 | A trigger request for a continuous recorder shall be rejected. | Test | Implemented |
| SW-API-010 | A start or stop request for a snapshot recorder shall be rejected. | Test | Implemented |
| SW-API-011 | `GET` and `POST /recording/<name>/status` shall return the status of the named recorder. | Test | Implemented |
| SW-API-012 | `GET` and `POST /recording/<name>/storage` shall return the storage status of the named recorder. | Test | Implemented |
| SW-API-013 | `GET` and `POST /recording/<name>/disk` shall return the free disk space reported by the named recorder. | Test | Implemented |
| SW-API-014 | `GET` and `POST /recording/<name>/prefix?prefix=<value>` shall set the prefix used by the next continuous start. | Test | Implemented |
| SW-API-015 | A prefix change shall leave an open continuous bag directory name unchanged. | Test | Implemented |
| SW-API-016 | A prefix change for a snapshot recorder shall be rejected. | Test | Implemented |
| SW-API-017 | `GET /recording/nodes` shall list the configured recorders whose service is available. | Test | Implemented |
| SW-API-018 | `GET /recording/status` shall return the status of every configured recorder. | Test | Implemented |
| SW-API-019 | A request for an unknown recorder name shall return HTTP 404. | Test | Implemented |
| SW-API-020 | A request for a recorder whose service is unavailable shall return HTTP 503. | Test | Implemented |
| SW-API-021 | When the recorder completes a call and rejects it, the REST service shall return HTTP 200 with `"success": false`. | Test | Implemented |
| SW-API-022 | The REST service shall not serve the operator page files. | Inspection | Implemented |

## Operator page

| ID | Requirement | Verification | Status |
| --- | --- | --- | --- |
| SW-HMI-001 | The operator page shall be provided as the static files `www/index.html`, `www/app.js`, and `www/style.css`. | Inspection | Implemented |
| SW-HMI-002 | The page shall use an operator-entered API address, with the default `http://127.0.0.1:8080`. | Test | Implemented |
| SW-HMI-003 | The page shall store the API address in the browser and shall reuse it on the next open. | Test | Implemented |
| SW-HMI-004 | The page shall refresh the recorder list and each recorder status every 2 seconds. | Test | Implemented |
| SW-HMI-005 | A continuous recorder card shall show the state, the directory, the free disk space, the rate-check result, and Start and Stop controls. | Inspection | Implemented |
| SW-HMI-006 | While continuous recording is active, the card shall show the start time and the duration. | Test | Implemented |
| SW-HMI-007 | A snapshot recorder card shall show the last trigger time and a Trigger control. | Inspection | Implemented |
| SW-HMI-008 | After Start, Stop, or Trigger, the page shall show `OK` when the command is accepted and `ERROR` when the command is rejected. | Test | Implemented |
| SW-HMI-011 | When a later rate-check result is `OK` or `ERROR` and differs from the result at the time of the command, the page shall show the rate-check result. | Test | Implemented |
| SW-HMI-012 | For a snapshot, the page shall show `OK` when the trigger is accepted, before the post-trigger interval has stored the MCAP file. | Test | Implemented |

## Delivered configuration

These requirements fix the values shipped in the parameter and launch files. A later change of a value changes the matching requirement.

| ID | Requirement | Verification | Status |
| --- | --- | --- | --- |
| SW-CFG-001 | The delivered continuous configuration shall set `split_size_mb` to 10000. | Inspection | Implemented |
| SW-CFG-002 | The delivered continuous configuration shall set `split_duration_s` to 0. | Inspection | Implemented |
| SW-CFG-003 | The delivered continuous configuration shall set `min_free_disk_mb` to 1024. | Inspection | Implemented |
| SW-CFG-004 | The delivered continuous configuration shall set `tolerance_topic_rate` to 5. | Inspection | Implemented |
| SW-CFG-005 | The delivered continuous configuration shall set `autostart_recording` to false. | Inspection | Implemented |
| SW-CFG-006 | The delivered continuous configuration shall set `publish_state_rate` to 1. | Inspection | Implemented |
| SW-CFG-007 | The delivered continuous configuration shall include `/tf_static` in `transient_local_topics`. | Inspection | Implemented |
| SW-CFG-008 | The delivered snapshot configuration shall set `snapshot_cache_mb` to 1024. | Inspection | Implemented |
| SW-CFG-009 | The delivered snapshot configuration shall set `snapshot_post_trigger_s` to 10. | Inspection | Implemented |
| SW-CFG-010 | The delivered snapshot configuration shall set `tolerance_topic_rate` to 5. | Inspection | Implemented |
| SW-CFG-011 | The delivered snapshot configuration shall omit `autostart_recording`. | Inspection | Implemented |
| SW-CFG-012 | The delivered MCAP options shall set compression to LZ4, compression level to Fastest, and chunk size to 786432. | Inspection | Implemented |
| SW-CFG-013 | The delivered continuous launch shall set the prefix to `continous`. | Inspection | Implemented |
| SW-CFG-014 | The delivered snapshot launch shall set the prefix to `snapshot`. | Inspection | Implemented |
| SW-CFG-015 | The delivered recorder launches shall set the output directory to the user home directory. | Inspection | Implemented |
| SW-CFG-016 | The delivered REST configuration shall bind address `0.0.0.0` and port `8080`. | Inspection | Implemented |
| SW-CFG-017 | The delivered REST configuration shall set `service_timeout_s` to 10. | Inspection | Implemented |
| SW-CFG-018 | The delivered REST configuration shall include the continuous entry `data_recorder:/system/data_recorder`. | Inspection | Implemented |
| SW-CFG-019 | The delivered REST configuration shall include the snapshot entry `snapshot:/system/data_recorder_snapshot`. | Inspection | Implemented |
