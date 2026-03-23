# MOVIN LiveLink Plugin for Unreal Engine 5

Receives real-time motion capture data from **MOVIN Studio** via UDP and feeds it into Unreal Engine's [LiveLink](https://dev.epicgames.com/documentation/en-us/unreal-engine/live-link-in-unreal-engine) system.

## Features

- Streams skeleton and animation data from MOVIN Studio over UDP
- Automatic skeleton change detection when switching characters
- Supports multi-character setups by using multiple MOVIN Studio instances on different UDP ports
- Works in both the **Editor** and **Packaged Games**
- Configurable UDP port (default: `11236`)

## Requirements

- Unreal Engine **5.3+** (5.3 ~ 5.7)
- Windows 64-bit
- MOVIN Studio (sending motion capture data)

## Installation

Copy the `MOVINLiveLinkPlugin` folder into your project's `Plugins/` directory:

```
YourProject/
├── Content/
├── Source/
├── Plugins/
│   └── MOVINLiveLinkPlugin/      ← copy here
│       ├── MOVINLiveLink.uplugin
│       ├── Source/
│       └── Resources/
└── YourProject.uproject
```

Rebuild your project. The plugin will be detected automatically.

## Quick Start (Editor)

1. Open your project in Unreal Editor
2. Go to **Window → Virtual Production → Live Link**
3. Click **+ Source → MOVIN LiveLink**
4. Set the UDP port (default `11236`) and click **Add**
5. Start streaming from MOVIN Studio — subjects will appear in the LiveLink panel

## Setting Up Your Character

To drive a Skeletal Mesh character with LiveLink data:

1. **Create an Animation Blueprint** for your character's Skeleton
2. In the **AnimGraph**, add a **Live Link Pose** node
3. If MOVIN Studio is streaming an **Actor**, receive it in Unreal using the `NeoMOVINMan_UE.fbx` model
4. If MOVIN Studio is streaming a **character model**, the model imported into Unreal must be the exact same `.fbx` used in MOVIN Studio
5. In the **Live Link Pose** node, select the subject that corresponds to the character you want to drive
6. Connect the Live Link Pose output to the **Output Pose**
7. Assign the Animation Blueprint to your Skeletal Mesh Actor

## Packaged Game Setup

The plugin supports running in packaged (shipped) games. Use the **Add MOVIN LiveLink Source** Blueprint node to create the source at runtime.

### 1. Enable the plugins

Make sure both **LiveLink** and **MOVINLiveLink** are enabled in your project:

- **Edit → Plugins** → search "Live Link" → enable
- **Edit → Plugins** → search "MOVIN" → enable

### 2. Call the Blueprint node at startup

#### Option A: Level Blueprint (simplest)

1. Open your level in the editor
2. Click **Blueprints** in the toolbar → **Open Level Blueprint**
3. Right-click in the graph → add an **Event BeginPlay** node
4. Drag from the BeginPlay execution pin → search and add **Add MOVIN LiveLink Source**
5. Set the **Port** parameter (default `11236`)
6. **Compile** and **Save**

```
[Event BeginPlay] ──▶ [Add MOVIN LiveLink Source]
                            Port: 11236
```

> **Note:** The source is re-created each time the level loads. If your game has multiple levels, add the node to each level's Blueprint.

#### Option B: GameInstance Blueprint (recommended)

Use this if you want the source to persist across level changes:

1. Content Browser → right-click → **Blueprint Class** → search **GameInstance** → create it (e.g. `BP_MyGameInstance`)
2. Open it → in the **Event Graph**, add an **Event Init** node
3. Drag from it → add **Add MOVIN LiveLink Source** (port `11236`)
4. **Compile** and **Save**
5. Go to **Edit → Project Settings → Maps & Modes → Game Instance Class** → set it to your `BP_MyGameInstance`

This runs once at game startup and survives level transitions.

### 3. Package

Package your game normally. Ensure MOVIN Studio is streaming to the configured port when the game runs.

## UDP Packet Format

The plugin expects binary UDP packets from MOVIN Studio in the following layout. All values are **little-endian**. Strings use **C# `BinaryWriter` 7-bit encoded length prefix**.

| Field                                              | Size                 | Description                        |
| -------------------------------------------------- | -------------------- | ---------------------------------- |
| `packetSize`                                     | 4 bytes (int32)      | Total size of the datagram body    |
| `subjectName`                                    | variable             | 7-bit length prefix + UTF-8 string |
| `frameIdx`                                       | 4 bytes (int32)      | Frame index                        |
| `boneCount`                                      | 4 bytes (int32)      | Number of bones                    |
| **Per bone** (repeated `boneCount` times): |                      |                                    |
| `boneName`                                       | variable             | 7-bit length prefix + UTF-8 string |
| `localPosition`                                  | 12 bytes (3× float) | X, Y, Z position                   |
| `localRotation`                                  | 16 bytes (4× float) | X, Y, Z, W quaternion              |
| `localScale`                                     | 12 bytes (3× float) | X, Y, Z scale                      |

> **Coordinate system:** The plugin converts from Unity's Y-up left-hand coordinate system (X, Y, Z) to Unreal's Z-up left-hand system (Z, X, Y) automatically.

## Multiple Characters

To stream multiple characters at the same time, use **one Tracin device and one MOVIN Studio instance per character**.

In Unreal, create multiple **MOVIN LiveLink Source** entries and assign each one a different UDP port such as `11236`, `11237`, and so on.

On each computer connected to a Tracin device, run **MOVIN Studio** and set its streaming port to match the corresponding LiveLink source port in Unreal.

In **MOVIN Studio**, set the **Streaming Host** field to the IP address of the computer running the Unreal project.

The computers running **Tracin + MOVIN Studio** and the computer running **Unreal** must be on the same network. They can be connected through the same router or Wi-Fi network.

With this setup, each MOVIN LiveLink source receives one character stream on its own port, allowing multiple characters to be used in the same Unreal project.

## Logging

The plugin logs under the `LogMOVINLiveLink` category. To see diagnostic messages in the Output Log, use the console command:

```
Log LogMOVINLiveLink Verbose
```

Key log events:

- New subject detected (first packet for a character)
- Skeleton changes (bone count or bone name changes)
- Parse failures and invalid datagrams

## Project Structure

```
MOVINLiveLinkPlugin/
├── MOVINLiveLink.uplugin
├── Resources/
│   └── Icon128.png
└── Source/
    └── MOVINLiveLink/
        ├── MOVINLiveLink.Build.cs
        ├── Public/
        │   ├── MOVINDatagram.h                  # Packet parsing
        │   ├── MOVINLiveLinkFunctionLibrary.h   # Blueprint function library
        │   ├── MOVINLiveLinkModule.h             # Plugin module + log category
        │   ├── MOVINLiveLinkSource.h             # LiveLink source (UDP receiver)
        │   ├── MOVINLiveLinkSourceEditor.h       # Editor UI (port selector)
        │   └── MOVINLiveLinkSourceFactory.h      # LiveLink source factory
        └── Private/
            ├── MOVINDatagram.cpp
            ├── MOVINLiveLinkFunctionLibrary.cpp
            ├── MOVINLiveLinkModule.cpp
            ├── MOVINLiveLinkSource.cpp
            ├── MOVINLiveLinkSourceEditor.cpp
            └── MOVINLiveLinkSourceFactory.cpp
```

## License

Copyright 2025 MOVIN. All Rights Reserved.
