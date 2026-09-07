# B-System Chat Architecture & Specification

## 1. Executive Summary

This document specifies the format, architecture, and implementation requirements
for porting the classic **BeOS Chat (Blabber)** application to the **BTRON 3.20 (B-System)** operating system.

The implementation is structured into clean C99 modules (`src/apps/chat.c` and
`src/apps/chat_xml.c`), integrating seamlessly with the B-TRON Window Manager,
Display Primitives (`dp.h`), TRON HMI, and **TRON IPC (Pub/Sub Message Bus)**.
It provides the **full set of windows and UI flows** of the original application,
using an in-memory TRON IPC protocol engine to mock XMPP/Jabber without requiring
external network daemons.

## 2. Window Architecture & UI Specification

The application implements the complete window set of original BeOS Chat:

```
+-------------------------------------------------------------------------+
|                          B-TRON CHAT WORKSPACE                          |
|                                                                         |
|  +-----------------------+   +---------------------------------------+  |
|  | [B] Blabber Roster    |   | [#] #btron-hall (MUC Groupchat)       |  |
|  +-----------------------+   +---------------------------------------+  |
|  | [Jabber] [Buddies]... |   | Topic: Welcome to B-TRON 3.20 Hall!   |  |
|  | Identity: Cyber-Samu. |   |---------------------------------------|  |
|  | Status: [● Online  v] |   | [12:00] <Retro-Hacker> Hello B-TRON!  |  |
|  |-----------------------|   | [12:01] <Cyber-Samurai> Hi all!      |  |
|  | v General (2/2)       |   | [12:02] * Sakamura-Sensei joined MUC  |  |
|  |   ● Retro-Hacker      |   |---------------------------------------|  |
|  |   ● Matrix-Monk       |   | > [Enter message here...      ] [Send]|  |
|  | v MUC Rooms (1/1)     |   +---------------------------------------+  |
|  |   # #btron-hall       |                                              |
|  |-----------------------|   +---------------------------------------+  |
|  | [Add Buddy] [Connect] |   | [@] Chat: Matrix-Monk (Private)       |  |
|  +-----------------------+   +---------------------------------------+  |
|                              | [12:05] <Matrix-Monk> Need BTRON spec |  |
|  +-----------------------+   | [12:06] <Cyber-Samurai> Sending TAD...|  |
|  | [Add Buddy Dialog]    |   |---------------------------------------|  |
|  | JID: user@btron.org   |   | > [Private message...         ] [Send]|  |
|  | Nick: Bit-Walker      |   +---------------------------------------+  |
|  | Group: General        |                                              |
|  | [Add]      [Cancel]   |                                              |
|  +-----------------------+                                              |
+-------------------------------------------------------------------------+
```

### 2.1 Window Definitions

| Window Name | Type | Dimensions | Description |
| :--- | :--- | :--- | :--- |
| **Main Roster Window** (`BlabberMainWindow`) | Primary Window | 240x460 | Displays user identity, presence status picker, buddy hierarchy groups (General, Work, MUC Rooms), connect/disconnect toggles, and main menu bar. |
| **MUC Groupchat Window** (`ChatWindow [MUC]`) | Multi-User Room | 540x360 | Room transcript with chronological chat history, colored handles, timestamp formatting, participant sidebar roster, and message composition input. |
| **Private Chat Window** (`ChatWindow [1-on-1]`) | 1-to-1 Chat | 440x320 | Dedicated peer-to-peer messaging window with direct recipient context, conversation logging, and active typing state. |
| **Add / Edit Buddy Dialog** (`BuddyWindow`) | Child Dialog | 320x220 | Form controls to input Jabber ID (`user@domain`), Nickname handle, and Group category. |
| **Preferences / Login Dialog** (`PreferencesWindow`) | Child Dialog | 360x260 | Custom Handle selection, Virtual Server host (`btron.org`), auto-connect settings, and MUC room bookmarks. |

## 3. Mock XMPP over TRON IPC (Pub/Sub Bus)

### 3.1 Component-Based Word-Nick Generator

Every newly spawned Chat window automatically receives a memorable,
cyberpunk-flavored component-based handle if not explicitly set:

$$\text{Nick} = \text{Prefix/Adjective} + \text{"-"} + \text{Noun/Role}$$

- **Prefixes (Adjectives)**: `Cyber`, `Retro`, `Neon`, `Matrix`, `Tron`, `Bit`, `Quantum`, `Pixel`, `Solar`, `Hyper`, `Silicon`, `Vector`, `Micro`, `B-Free`, `T-Kernel`
- **Roles (Nouns)**: `Samurai`, `Hacker`, `Pilot`, `Monk`, `Ninja`, `Walker`, `Ronin`, `Knight`, `Wizard`, `Courier`, `Guru`, `Sensei`, `Daemon`, `Crafter`
- *Examples*: `Cyber-Samurai`, `Retro-Hacker`, `Tron-Ninja`, `Bit-Walker`, `Matrix-Monk`, `Neon-Pilot`.

### 3.2 TRON IPC Pub/Sub Architecture
The in-memory IPC broker operates as a multi-client broadcast/unicast bus using TRON task communication concepts:
1. **Client Registration**: Each Chat instance registers its `client_id`, `nick_handle`, `jid` (`<nick>@btron.org`), and callback event queue.
2. **MUC Room Broadcast (`#btron-hall`, `#general`)**: Messages sent to a groupchat room are multiplexed to all active instances subscribed to that room.
3. **1-to-1 Private Message Routing**: Direct stanzas sent to `<target_nick>@btron.org` are routed strictly to the target instance.
4. **Presence Synchronization**: Broadcasting `<presence>` stanzas updates all open roster windows in real-time when clients join, change status (Online/Away/Busy/Offline), or exit.
5. **Simulated Virtual Chatters (Bot Participants)**: When running standalone, virtual agents (`Ken-Sakamura`, `Tron-Daemon`, `BeOS-Blabber`) participate in the MUC room and respond to user messages and greetings.

---

## 4. XML Stanza Protocol Format (`chat_xml.c`)

All TRON IPC messages carry standardized XMPP XML stanzas parsed and generated by the lightweight C99 XML engine:

### 4.1 Presence Stanza
```xml
<presence from="Cyber-Samurai@btron.org/desktop" to="#btron-hall">
  <show>online</show>
  <status>Hacking on B-TRON 3.20!</status>
  <priority>10</priority>
</presence>
```

### 4.2 Groupchat Message Stanza (MUC)
```xml
<message from="Retro-Hacker@btron.org/desktop" to="#btron-hall" type="groupchat">
  <body>Hello everyone! Welcome to the B-TRON BeOS Chat!</body>
  <stamp>12:04:15</stamp>
</message>
```

### 4.3 Direct 1-to-1 Private Message Stanza
```xml
<message from="Cyber-Samurai@btron.org/desktop" to="Matrix-Monk@btron.org" type="chat">
  <body>Can you review the TAD document specification?</body>
  <stamp>12:05:30</stamp>
</message>
```

### 4.4 Roster IQ Query Stanza
```xml
<iq from="Cyber-Samurai@btron.org" type="result" id="roster_1">
  <query xmlns="jabber:iq:roster">
    <item jid="Retro-Hacker@btron.org" name="Retro Hacker" subscription="both">
      <group>General</group>
    </item>
    <item jid="Matrix-Monk@btron.org" name="Matrix Monk" subscription="both">
      <group>Developers</group>
    </item>
  </query>
</iq>
```

## 5. File & Component Breakdown

1. **`include/btron/chat.h`**:
   - Data structures (`ChatClient`, `ChatWindowContext`, `RosterItem`, `ChatMessage`, `ChatRoom`).
   - TRON IPC Pub/Sub prototypes and constants.
2. **`src/apps/chat_xml.c`**:
   - Lightweight XML reader/writer (`ChatXmlNode`, parsing, formatting, escaping, memory recycling).
   - Stanza encoders and decoders for `<message>`, `<presence>`, `<iq>`.
3. **`src/apps/chat.c`**:
   - Window lifecycle: `open_chat_roster_window()`, `open_chat_muc_window()`, `open_chat_private_window()`, `open_chat_add_buddy_dialog()`.
   - Complete painting routines adhering to B-TRON retro aesthetics and BeOS compact sliding tabs.
   - Event handlers for keyboard text input, scrollbars, list selection, double-click to chat, and status changes.
   - TRON IPC Pub/Sub broker integration and mock message pump.
4. **`src/apps/test_tad_browser.c` / `src/apps/test_chat.c`**:
   - Automated unit test suite verifying client registration, random nick generation, XML stanza round-trip parsing, MUC broadcast, and private routing.

## 6. NASA JPL Safety & Quality Rules Compliance

- **No Unbounded Loops**: All string operations, XML tokenization loops, and message queues have strict upper limits (`MAX_CHAT_MSG = 256`, `MAX_ROSTER = 64`, `MAX_XML_DEPTH = 8`).
- **Deterministic Memory**: Fixed capacity ring buffers for message logs and roster tables without runtime fragmentation.
- **Fail-Safe Defaults**: Non-blocking IPC message dispatch and graceful handling of disconnected or closed windows.

# Credits

* Namdak Tonpa and Grok 4.5
