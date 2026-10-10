#pragma once
#include <stdint.h>

/* Experimental, separate Zig world core. This is not the EF Link transport API.
 * Calls on one context must be serialized. No function calls the game or sends
 * official traffic. Input protobuf buffers are borrowed only during a call;
 * poll copies a standard server body into caller-owned memory.
 * Member IDs must come from the authenticated EF Link connection. Character
 * templates are host-approved server input, never guest HP/settlement claims.
 * Native-applied proofs must come from a trusted game-thread scene adapter.
 * That adapter, full combat, restoration and real-client integration are pending.
 */
#ifdef _WIN32
#define EFLINK_NATIVE_CALL __cdecl
#else
#define EFLINK_NATIVE_CALL
#endif

#define EFLINK_NATIVE_ENTRY "EFLinkNative_GetApiV1"
typedef struct EFLinkNative_Context EFLinkNative_Context;
enum EFLinkNative_Result {
    EFLinkNative_Ok=0, EFLinkNative_InvalidArgument=1,
    EFLinkNative_Rejected=2, EFLinkNative_OutOfMemory=3,
    EFLinkNative_BufferTooSmall=4, EFLinkNative_Empty=5
};
typedef struct EFLinkNative_Message {
    uint64_t world_id, transition_id;
    uint32_t message_id, size;
    uint16_t recipient;
    uint16_t reserved;
    uint32_t reserved2;
} EFLinkNative_Message;
typedef struct EFLinkNative_Proof {
    uint64_t transition_id, world_id, scene_id, leader_id, private_feed_generation;
} EFLinkNative_Proof;
typedef struct EFLinkNative_ApiV1 {
    uint32_t struct_size, version;
    int32_t (EFLINK_NATIVE_CALL *create)(uint64_t world_id,uint64_t scene_id,int32_t scene_num,EFLinkNative_Context** out);
    void (EFLINK_NATIVE_CALL *destroy)(EFLinkNative_Context*);
    int32_t (EFLINK_NATIVE_CALL *add_member)(EFLinkNative_Context*,uint16_t member);
    int32_t (EFLINK_NATIVE_CALL *import_host_scene)(EFLinkNative_Context*,uint16_t member,const uint8_t*,uint32_t size);
    int32_t (EFLINK_NATIVE_CALL *allocate_approved_operator)(EFLinkNative_Context*,uint16_t member,const uint8_t*,uint32_t size,uint64_t* out_id);
    int32_t (EFLINK_NATIVE_CALL *begin_scene)(EFLinkNative_Context*,uint16_t member,uint64_t native_role_id,uint64_t private_generation,uint64_t* out_transition);
    int32_t (EFLINK_NATIVE_CALL *scene_loaded)(EFLinkNative_Context*,uint16_t member,uint64_t world_id,uint64_t transition,const uint8_t*,uint32_t size);
    int32_t (EFLINK_NATIVE_CALL *native_applied)(EFLinkNative_Context*,uint16_t member,const EFLinkNative_Proof*);
    /* A size query/short buffer leaves the front message queued. */
    int32_t (EFLINK_NATIVE_CALL *poll)(EFLinkNative_Context*,EFLinkNative_Message*,uint8_t* destination,uint32_t capacity);
    /* Disconnect cancels this guest's pending messages and shared operators.
     * The game adapter must separately restore its private world. Destroy the
     * whole context when the host disconnects. Transition IDs survive ID reuse.
     */
    int32_t (EFLINK_NATIVE_CALL *remove_member)(EFLinkNative_Context*,uint16_t member);
} EFLinkNative_ApiV1;
typedef const EFLinkNative_ApiV1* (EFLINK_NATIVE_CALL *EFLinkNative_GetApiV1Fn)(void);

#ifdef __cplusplus
static_assert(sizeof(EFLinkNative_Message)==32);
static_assert(sizeof(EFLinkNative_Proof)==40);
#endif
