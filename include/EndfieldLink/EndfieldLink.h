#pragma once
/*
 * EF Link public API, version 1.
 *
 * EF Link (module id "eflink.core") is a LAN multiplayer layer for Arknights: Endfield
 * running under Better-Endfield. It finds rooms on the local network, keeps players
 * connected, shows every player's operator in everyone else's game and carries data for
 * other modules over named channels.
 *
 * Using it from another Better-Endfield module:
 *   1. List "eflink.core" in your module.json "dependencies" so it loads first.
 *   2. Look the API up once it is loaded:
 *        HMODULE link=GetModuleHandleW(L"eflink.core.dll");
 *        auto get=(EFLink_GetApiV1Fn)GetProcAddress(link,EFLINK_ENTRY_V1);
 *        const EFLink_ApiV1* api=get?get():nullptr;
 *      Check api->struct_size and api->version before using later fields.
 *   3. Open a channel with a name unique to your module ("mymod.blocks") and send data.
 *      Every member that opened the same channel name receives it.
 *
 * Threads: every function may be called from any thread. Receive and peer callbacks run on
 * EF Link's network thread; copy what you need and return quickly, and never call Unity or
 * game methods from inside a callback.
 *
 * Strings are UTF-8. Buffers passed in are only borrowed for the duration of the call.
 */
#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#define EFLINK_CALL __cdecl
#else
#define EFLINK_CALL
#endif

#define EFLINK_API_VERSION_1 1u
#define EFLINK_ENTRY_V1 "EFLink_GetApiV1"
#define EFLINK_EVERYONE 0xFFFFu   /* send to every other member */
#define EFLINK_HOST_ID 1u         /* the room host always has id 1 */

typedef enum EFLink_Result {
    EFLink_Ok=0,
    EFLink_InvalidArgument=1,
    EFLink_NotConnected=2,
    EFLink_NotFound=3,
    EFLink_TooLarge=4,
    EFLink_Busy=5,
    EFLink_Failed=6
} EFLink_Result;

typedef enum EFLink_Role {
    EFLink_RoleOffline=0,      /* not in a room */
    EFLink_RoleConnecting=1,   /* joining a room */
    EFLink_RoleHost=2,         /* hosting a room */
    EFLink_RoleClient=3        /* member of someone else's room */
} EFLink_Role;

typedef enum EFLink_SendFlags {
    EFLink_Unreliable=0,       /* UDP: may be lost or reordered, at most 1024 bytes */
    EFLink_Reliable=1          /* TCP: delivered in order, at most 256 KiB */
} EFLink_SendFlags;

typedef struct EFLink_SessionInfo {
    uint32_t struct_size;      /* set by the caller to sizeof(EFLink_SessionInfo) */
    EFLink_Role role;
    uint16_t local_id;         /* 0 while offline */
    uint16_t player_count;     /* including the local player */
    uint32_t room_id;
    char room_name[128];
    char local_name[128];
} EFLink_SessionInfo;

typedef struct EFLink_PeerInfo {
    uint32_t struct_size;      /* set by the caller to sizeof(EFLink_PeerInfo) */
    uint16_t id;
    uint8_t is_local;
    uint8_t is_host;
    uint8_t same_map;          /* standing in the same map as the local player */
    uint8_t has_state;         /* a recent pose has been received */
    uint16_t reserved;
    uint32_t rtt_ms;           /* round trip to the host, 0 when unknown */
    float position[3];         /* Endfield world units */
    float yaw_degrees;
    char name[128];
    char map_id[64];
    char model_path[512];
} EFLink_PeerInfo;

/* data is borrowed for the callback only. */
typedef void (EFLINK_CALL* EFLink_ReceiveFn)(void* user,uint16_t from_peer,const void* data,uint32_t size);
/* joined: 1 when the peer joined (or the local player entered a room), 0 when it left. */
typedef void (EFLINK_CALL* EFLink_PeerFn)(void* user,uint16_t peer,int joined);

typedef struct EFLink_ApiV1 {
    uint32_t struct_size;
    uint32_t version;

    EFLink_Result (EFLINK_CALL* get_session)(EFLink_SessionInfo* out);
    /* Writes up to capacity entries and returns the number of players in the room. */
    uint32_t (EFLINK_CALL* list_peers)(EFLink_PeerInfo* out,uint32_t capacity);

    /* Opens a named channel; the handle is used for sending and closing. */
    EFLink_Result (EFLINK_CALL* open_channel)(const char* name,EFLink_ReceiveFn receive,void* user,uint32_t* handle);
    EFLink_Result (EFLINK_CALL* close_channel)(uint32_t handle);
    EFLink_Result (EFLINK_CALL* send)(uint32_t handle,uint16_t to_peer,const void* data,uint32_t size,uint32_t flags);

    /* Peer join/leave notifications; returns a handle for remove_peer_listener. */
    EFLink_Result (EFLINK_CALL* add_peer_listener)(EFLink_PeerFn callback,void* user,uint32_t* handle);
    EFLink_Result (EFLINK_CALL* remove_peer_listener)(uint32_t handle);

    /* Room control, same as the buttons in the EF Link native menu.
       options_json: {"room_name":"..","max_players":8,"password":"..","port":47615} */
    EFLink_Result (EFLINK_CALL* host_room)(const char* options_json);
    /* address: "host[:port]"; password may be null. */
    EFLink_Result (EFLINK_CALL* join_room)(const char* address,const char* password);
    EFLink_Result (EFLINK_CALL* leave_room)(void);
} EFLink_ApiV1;

typedef const EFLink_ApiV1* (EFLINK_CALL* EFLink_GetApiV1Fn)(void);

/* V2 preserves V1 and adds world-session access. Returned JSON is a copied snapshot;
   it contains no native addresses. get_world_json returns required bytes including NUL.
   request_teleport queues a request; completion is asynchronous and reported in JSON.
   session.ready means fresh snapshot data, NOT admission to an authoritative world.
   Check session.entry.active; incomplete native adapters cannot admit players.
   World schema 1 uses map/level, epoch/revision and stable entity keys; a later external
   adapter can translate these into its own coordinates without changing transport. */
#define EFLINK_API_VERSION_2 2u
#define EFLINK_ENTRY_V2 "EFLink_GetApiV2"
typedef struct EFLink_ApiV2 {
    uint32_t struct_size;
    uint32_t version;
    const EFLink_ApiV1* transport;
    uint32_t (EFLINK_CALL* get_world_json)(char* destination,uint32_t capacity);
    EFLink_Result (EFLINK_CALL* request_teleport)(uint16_t target_peer);
} EFLink_ApiV2;
typedef const EFLink_ApiV2* (EFLINK_CALL* EFLink_GetApiV2Fn)(void);
