cmake_minimum_required(VERSION 3.16)
project(virt_companion CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)

# ---- Fetch rweather/Crypto (AES128, Ed25519, Curve25519, SHA256/512) ----------
include(FetchContent)
FetchContent_Declare(
    arduinolibs
    GIT_REPOSITORY https://github.com/rweather/arduinolibs.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(arduinolibs)
set(CRYPTO_DIR ${arduinolibs_SOURCE_DIR}/libraries/Crypto)

# ---- Include paths -----------------------------------------------------------
set(ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../..)
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/shims      # must come before src/ to override Arduino-specific headers
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${ROOT_DIR}/src
    ${ROOT_DIR}/lib/ed25519
    ${CRYPTO_DIR}
)

# ---- Sources -----------------------------------------------------------------

# ed25519 reference implementation (keypair, sign, verify, key exchange)
file(GLOB ED25519_SRCS ${ROOT_DIR}/lib/ed25519/*.c)

# rweather/Crypto (AES, Ed25519 class, Curve25519, SHA256/512, Hash, …)
file(GLOB CRYPTO_SRCS ${CRYPTO_DIR}/*.cpp)

# MeshCore portable core
set(CORE_SRCS
    ${ROOT_DIR}/src/Dispatcher.cpp
    ${ROOT_DIR}/src/Identity.cpp
    ${ROOT_DIR}/src/Mesh.cpp
    ${ROOT_DIR}/src/Packet.cpp
    ${ROOT_DIR}/src/Utils.cpp
    ${ROOT_DIR}/src/helpers/AdvertDataHelpers.cpp
    ${ROOT_DIR}/src/helpers/BaseChatMesh.cpp
    ${ROOT_DIR}/src/helpers/CommonCLI.cpp
    ${ROOT_DIR}/src/helpers/RegionMap.cpp
    ${ROOT_DIR}/src/helpers/StaticPoolPacketManager.cpp
    ${ROOT_DIR}/src/helpers/TransportKeyStore.cpp
    ${ROOT_DIR}/src/helpers/TxtDataHelpers.cpp
    ${ROOT_DIR}/src/helpers/linux/LinuxDataStore.cpp
    ${ROOT_DIR}/src/helpers/linux/LinuxTCPInterface.cpp
    ${ROOT_DIR}/src/helpers/linux/MQTTRadio.cpp
)

# Application
set(APP_SRCS
    main.cpp
    target.cpp
    MyMesh.cpp
)

add_executable(virt_companion
    ${APP_SRCS}
    ${CORE_SRCS}
    ${ED25519_SRCS}
    ${CRYPTO_SRCS}
)

target_compile_definitions(virt_companion PRIVATE
    NDEBUG
    ENABLE_PRIVATE_KEY_IMPORT=1
    ENABLE_PRIVATE_KEY_EXPORT=1
    LORA_FREQ=915
    LORA_BW=250
    LORA_SF=10
    LORA_CR=5
    LORA_TX_POWER=20
    MAX_CONTACTS=100
    OFFLINE_QUEUE_SIZE=16
    MAX_GROUP_CHANNELS=8
)

target_link_libraries(virt_companion PRIVATE
    mosquitto
    pthread
)
