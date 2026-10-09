// Test of the live resize API without a connection and of the SDP parse.
//
// Build the library once in the moonlight toolbox:
//   cmake -S . -B /tmp/mcc-build -DBUILD_SHARED_LIBS=OFF -DCMAKE_BUILD_TYPE=Debug
//   cmake --build /tmp/mcc-build -j
// Then:
//   gcc -O0 -g -I src -I enet/include test/resize_protocol_test.c \
//       /tmp/mcc-build/libmoonlight-common-c.a /tmp/mcc-build/enet/libenet.a \
//       -lcrypto -lpthread -o /tmp/resize_protocol_test && /tmp/resize_protocol_test
#include "Limelight-internal.h"
#include <stdio.h>
#include <string.h>

static int failures;

static void check(bool condition, const char* what) {
    if (!condition) {
        failures++;
        printf("FAIL: %s\n", what);
    }
}

int main(void) {
    uint32_t requestId = 77;
    uint32_t value = 5;

    // Without a connection the host cannot have advertised the feature
    check(!LiIsLiveResizeSupported(), "LiIsLiveResizeSupported() is true before a connection");

    // Without a connection the request must fail and leave the id alone
    check(LiSendResizeRequest(2536, 1390, &requestId) == -1, "LiSendResizeRequest() did not fail without a connection");
    check(requestId == 77, "LiSendResizeRequest() changed the id on failure");

    // The SDP attribute parse used by the DESCRIBE handler
    const char* sdpWith = "a=x-ss-general.featureFlags:3\r\na=x-ss-general.liveResize:1\r\na=rtpmap:96 opus/48000/1\r\n";
    const char* sdpWithout = "a=x-ss-general.featureFlags:3\r\na=rtpmap:96 opus/48000/1\r\n";
    check(parseSdpAttributeToUInt(sdpWith, "x-ss-general.liveResize", &value) && value == 1,
          "liveResize attribute not parsed as 1");
    value = 5;
    check(!parseSdpAttributeToUInt(sdpWithout, "x-ss-general.liveResize", &value) && value == 5,
          "missing liveResize attribute reported as present");

    // A new connection clears the value of the previous connection. The start
    // fails at the audio configuration check, before any network operation.
    {
        SERVER_INFORMATION serverInfo;
        STREAM_CONFIGURATION streamConfig;

        LiInitializeServerInformation(&serverInfo);
        serverInfo.address = "127.0.0.1";
        serverInfo.serverInfoAppVersion = "7.1.431.-1";
        serverInfo.serverCodecModeSupport = SCM_H264;
        LiInitializeStreamConfiguration(&streamConfig);
        streamConfig.audioConfiguration = 0;

        LiveResizeSupported = true;
        check(LiStartConnection(&serverInfo, &streamConfig, NULL, NULL, NULL, NULL, 0, NULL, 0) != 0,
              "LiStartConnection() did not fail with an incorrect audio configuration");
        check(!LiIsLiveResizeSupported(), "LiStartConnection() did not clear the live resize value");
    }

    if (failures != 0) {
        printf("%d checks failed\n", failures);
        return 1;
    }

    printf("resize_protocol_test: all checks passed\n");
    return 0;
}
