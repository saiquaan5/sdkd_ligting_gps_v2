idf_component_register(SRCS "app_pppos.c"
                    "app_main.c"
                    "app_network.c"
                    "app_websocket.c"
                    "app_ota.c"
                    "app_modbus.c"
                    "app_nextion.c"
                    "app_storage.c"
                    "app_schedule.c"
                    "app_controller.c"
                    "app_common.c"
                    "app_gps.c"
                    "i2cdev.c"
                    "ds3231.c"
                    "modbus_params.c"
                    "protobuf/common_proto.pb-c.c"
                    "protobuf/device_proto.pb-c.c"
                    "protobuf/main_proto.pb-c.c"
                    "protobuf/user_proto.pb-c.c"
                    INCLUDE_DIRS "." "protobuf" "include")
