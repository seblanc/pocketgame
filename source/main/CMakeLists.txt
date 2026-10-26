idf_component_register(
    SRCS "main.c" "i2cbus.c" "display.c" "touch.c" "sound.c"
         "ui.c" "settings.c" "battery.c" "net.c" "scroll.c"
         "game/tictactoe.c" "game/connect4.c"
    INCLUDE_DIRS "."
)
