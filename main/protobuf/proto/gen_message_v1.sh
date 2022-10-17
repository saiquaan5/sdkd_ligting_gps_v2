#cpp
protoc  --c_out=../ common_proto.proto
protoc  --c_out=../ user_proto.proto
protoc  --c_out=../ device_proto.proto
protoc  --c_out=../ main_proto.proto

# sed -i '' -e 's/protobuf-c\/protobuf-c.h/protobuf-c.h/' '../common-message-v1.pb-c.h'
# sed -i '' -e 's/protobuf-c\/protobuf-c.h/protobuf-c.h/' '../client-message-v1.pb-c.h'
# sed -i '' -e 's/protobuf-c\/protobuf-c.h/protobuf-c.h/' '../client-main-message-v1.pb-c.h'
