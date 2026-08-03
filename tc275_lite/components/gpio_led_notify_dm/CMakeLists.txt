ulmk_component_register(
    NAME         gpio_led_notify_dm
    ENABLED      OFF
    SOURCES
        src/root_thread.c
    REQUIRES
        ulmk_device_manager
        ulmk_device_classes
    ROOT_THREAD
)
