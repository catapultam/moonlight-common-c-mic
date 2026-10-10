// Test of the adaptive bitrate API without a connection, the SDP parse, the
// BITRATE_STATUS parse and the frame counters.
//
// Build the library once in the moonlight toolbox:
//   cmake -S . -B /tmp/mcc-build -DBUILD_SHARED_LIBS=OFF -DCMAKE_BUILD_TYPE=Debug
//   cmake --build /tmp/mcc-build -j
// Then:
//   gcc -O0 -g -I src -I enet/include test/bitrate_protocol_test.c \
//       /tmp/mcc-build/libmoonlight-common-c.a /tmp/mcc-build/enet/libenet.a \
//       -lcrypto -lpthread -o /tmp/bitrate_protocol_test && /tmp/bitrate_protocol_test
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
    uint32_t finished = 1, lost = 1;
    BITRATE_STATUS_PAYLOAD status;

    // Without a connection the host cannot have advertised the feature
    check(!LiIsDynamicBitrateSupported(), "LiIsDynamicBitrateSupported() is true before a connection");

    // Without a connection the request must fail and leave the id alone
    check(LiSendBitrateRequest(20000, &requestId) == -1, "LiSendBitrateRequest() did not fail without a connection");
    check(requestId == 77, "LiSendBitrateRequest() changed the id on failure");

    // The SDP attribute parse used by the DESCRIBE handler
    {
        const char* sdpWith = "a=x-ss-general.featureFlags:3\r\na=x-ss-general.dynamicBitrate:1\r\na=rtpmap:96 opus/48000/1\r\n";
        const char* sdpWithout = "a=x-ss-general.featureFlags:3\r\na=x-ss-general.liveResize:1\r\n";
        check(parseSdpAttributeToUInt(sdpWith, "x-ss-general.dynamicBitrate", &value) && value == 1,
              "dynamicBitrate attribute not parsed as 1");
        value = 5;
        check(!parseSdpAttributeToUInt(sdpWithout, "x-ss-general.dynamicBitrate", &value) && value == 5,
              "missing dynamicBitrate attribute reported as present");
    }

    // BITRATE_STATUS payload: id 1, requested 10000, accepted 10000, encoder 6000, APPLIED_RESTART
    {
        const char payload[20] = { 0x01, 0x00, 0x00, 0x00, 0x10, 0x27, 0x00, 0x00, 0x10, 0x27, 0x00, 0x00,
                                   0x70, 0x17, 0x00, 0x00, 0x01, 0x00, 0x55, 0x55 };
        memset(&status, 0, sizeof(status));
        check(parseBitrateStatusPayload(payload, 18, &status), "an 18 byte status was not parsed");
        check(status.requestId == 1 && status.requestedKbps == 10000 && status.acceptedKbps == 10000 &&
              status.encoderKbps == 6000 && status.status == LI_BITRATE_STATUS_APPLIED_RESTART,
              "status fields are wrong");
        check(parseBitrateStatusPayload(payload, 20, &status), "a longer status was not parsed");

        // A short payload must be dropped
        check(!parseBitrateStatusPayload(payload, 17, &status), "a 17 byte status was parsed");
        check(!parseBitrateStatusPayload(payload, 0, &status), "an empty status was parsed");
    }

    // Initialize the control stream as for a Sunshine host with an encrypted control
    // stream. This creates the counter lock. No network operation occurs.
    AppVersionQuad[0] = 7;
    AppVersionQuad[1] = 1;
    AppVersionQuad[2] = 431;
    AppVersionQuad[3] = -1;
    check(initializeControlStream() == 0, "initializeControlStream() failed");

    // The configuredKbps == 0 guard. The test cannot capture the bytes on the wire or
    // tell this guard from the peer == NULL guard: both return -1 with no peer.
    {
        DynamicBitrateSupported = true;
        requestId = 77;
        check(LiSendBitrateRequest(0, &requestId) == -1, "LiSendBitrateRequest(0) did not fail");
        check(requestId == 77, "LiSendBitrateRequest(0) changed the id");
        check(LiSendBitrateRequest(20000, &requestId) == -1, "LiSendBitrateRequest() did not fail with no peer");
        check(requestId == 77, "LiSendBitrateRequest() with no peer changed the id");
        DynamicBitrateSupported = false;
    }

    // Finished and lost frames. Frame 3 never arrives, frame 4 starts but does not complete.
    {
        LiGetVideoFrameCounters(&finished, &lost);
        check(finished == 0 && lost == 0, "frame counters do not start at 0");

        connectionSawFrame(1);
        connectionReceivedCompleteFrame(1, false);
        connectionSawFrame(2);
        connectionReceivedCompleteFrame(2, false);
        LiGetVideoFrameCounters(&finished, &lost);
        check(finished == 1 && lost == 0, "frame 1 is not counted as finished when frame 2 starts");

        connectionSawFrame(4);
        connectionSawFrame(5);
        LiGetVideoFrameCounters(&finished, &lost);
        check(finished == 4 && lost == 2, "frames 3 and 4 are not counted as lost");

        // The same frame again counts nothing
        connectionSawFrame(5);
        LiGetVideoFrameCounters(&finished, &lost);
        check(finished == 4 && lost == 2, "a repeated frame index changed the counters");

        // A new control stream sets the counters to 0. The second initialization
        // without destroyControlStream() leaks the first objects; this is a test only.
        check(initializeControlStream() == 0, "the second initializeControlStream() failed");
        LiGetVideoFrameCounters(&finished, &lost);
        check(finished == 0 && lost == 0, "initializeControlStream() did not set the frame counters to 0");

        // The count starts again from the first frame of the new stream
        connectionSawFrame(10);
        connectionSawFrame(12);
        LiGetVideoFrameCounters(&finished, &lost);
        check(finished == 2 && lost == 2, "the frame counters are wrong after the reset");
    }

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

        DynamicBitrateSupported = true;
        check(LiStartConnection(&serverInfo, &streamConfig, NULL, NULL, NULL, NULL, 0, NULL, 0) != 0,
              "LiStartConnection() did not fail with an incorrect audio configuration");
        check(!LiIsDynamicBitrateSupported(), "LiStartConnection() did not clear the dynamic bitrate value");
    }

    if (failures != 0) {
        printf("%d checks failed\n", failures);
        return 1;
    }

    printf("bitrate_protocol_test: all checks passed\n");
    return 0;
}
