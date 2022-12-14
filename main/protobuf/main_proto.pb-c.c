/* Generated by the protocol buffer compiler.  DO NOT EDIT! */
/* Generated from: main_proto.proto */

/* Do not generate deprecated warnings for self */
#ifndef PROTOBUF_C__NO_DEPRECATED
#define PROTOBUF_C__NO_DEPRECATED
#endif

#include "main_proto.pb-c.h"
void   main_message__init
                     (MainMessage         *message)
{
  static const MainMessage init_value = MAIN_MESSAGE__INIT;
  *message = init_value;
}
size_t main_message__get_packed_size
                     (const MainMessage *message)
{
  assert(message->base.descriptor == &main_message__descriptor);
  return protobuf_c_message_get_packed_size ((const ProtobufCMessage*)(message));
}
size_t main_message__pack
                     (const MainMessage *message,
                      uint8_t       *out)
{
  assert(message->base.descriptor == &main_message__descriptor);
  return protobuf_c_message_pack ((const ProtobufCMessage*)message, out);
}
size_t main_message__pack_to_buffer
                     (const MainMessage *message,
                      ProtobufCBuffer *buffer)
{
  assert(message->base.descriptor == &main_message__descriptor);
  return protobuf_c_message_pack_to_buffer ((const ProtobufCMessage*)message, buffer);
}
MainMessage *
       main_message__unpack
                     (ProtobufCAllocator  *allocator,
                      size_t               len,
                      const uint8_t       *data)
{
  return (MainMessage *)
     protobuf_c_message_unpack (&main_message__descriptor,
                                allocator, len, data);
}
void   main_message__free_unpacked
                     (MainMessage *message,
                      ProtobufCAllocator *allocator)
{
  if(!message)
    return;
  assert(message->base.descriptor == &main_message__descriptor);
  protobuf_c_message_free_unpacked ((ProtobufCMessage*)message, allocator);
}
static const ProtobufCFieldDescriptor main_message__field_descriptors[2] =
{
  {
    "userMessage",
    1,
    PROTOBUF_C_LABEL_NONE,
    PROTOBUF_C_TYPE_MESSAGE,
    0,   /* quantifier_offset */
    offsetof(MainMessage, usermessage),
    &user_message__descriptor,
    NULL,
    0,             /* flags */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
  {
    "deviceMessage",
    2,
    PROTOBUF_C_LABEL_NONE,
    PROTOBUF_C_TYPE_MESSAGE,
    0,   /* quantifier_offset */
    offsetof(MainMessage, devicemessage),
    &device_message__descriptor,
    NULL,
    0,             /* flags */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
};
static const unsigned main_message__field_indices_by_name[] = {
  1,   /* field[1] = deviceMessage */
  0,   /* field[0] = userMessage */
};
static const ProtobufCIntRange main_message__number_ranges[1 + 1] =
{
  { 1, 0 },
  { 0, 2 }
};
const ProtobufCMessageDescriptor main_message__descriptor =
{
  PROTOBUF_C__MESSAGE_DESCRIPTOR_MAGIC,
  "MainMessage",
  "MainMessage",
  "MainMessage",
  "",
  sizeof(MainMessage),
  2,
  main_message__field_descriptors,
  main_message__field_indices_by_name,
  1,  main_message__number_ranges,
  (ProtobufCMessageInit) main_message__init,
  NULL,NULL,NULL    /* reserved[123] */
};
