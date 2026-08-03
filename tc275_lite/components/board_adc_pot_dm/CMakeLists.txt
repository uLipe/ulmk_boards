ulmk_component_register(
    NAME         board_adc_pot_dm
    ENABLED      OFF
    SOURCES
        src/root_thread.c
    REQUIRES
        ulmk_device_manager
        ulmk_device_classes
    ROOT_THREAD
)
