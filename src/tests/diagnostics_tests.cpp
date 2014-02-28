#include <check.h>
#include <stdint.h>
#include "signals.h"
#include "diagnostics.h"

namespace diagnostics = openxc::diagnostics;
namespace usb = openxc::interface::usb;

using openxc::signals::getCanBuses;
using openxc::signals::getCanBusCount;
using openxc::signals::getMessages;
using openxc::pipeline::Pipeline;

extern diagnostics::DiagnosticsManager DIAGNOSTICS_MANAGER;
extern Pipeline PIPELINE;
extern UsbDevice USB_DEVICE;

extern long FAKE_TIME;

QUEUE_TYPE(uint8_t)* OUTPUT_QUEUE = &PIPELINE.usb->endpoints[IN_ENDPOINT_INDEX].queue;

DiagnosticRequest request = {
    arbitration_id: 0x7e0,
    mode: OBD2_MODE_POWERTRAIN_DIAGNOSTIC_REQUEST,
    has_pid: true,
    pid: 0x2,
    pid_length: 1
};
CanMessage message = {request.arbitration_id + 0x8, __builtin_bswap64(0x341024500000000)};

static bool canQueueEmpty(int bus) {
    return QUEUE_EMPTY(CanMessage, &getCanBuses()[bus].sendQueue);
}

bool outputQueueEmpty() {
    return QUEUE_EMPTY(uint8_t, OUTPUT_QUEUE);
}

static void resetQueues() {
    usb::initialize(&USB_DEVICE);
    PIPELINE.usb->configured = true;
    for(int i = 0; i < getCanBusCount(); i++) {
        openxc::can::initializeCommon(&getCanBuses()[i]);
        fail_unless(canQueueEmpty(i));
    }
    fail_unless(outputQueueEmpty());
}

void setup() {
    PIPELINE.outputFormat = openxc::pipeline::JSON;
    PIPELINE.usb = &USB_DEVICE;
    resetQueues();
    diagnostics::initialize(&DIAGNOSTICS_MANAGER, getCanBuses(),
            getCanBusCount());
}

START_TEST (test_add_recurring_too_frequent)
{
    ck_assert(diagnostics::addDiagnosticRequest(&DIAGNOSTICS_MANAGER,
            &getCanBuses()[0], &request, 1));
    ck_assert(diagnostics::addDiagnosticRequest(&DIAGNOSTICS_MANAGER,
            &getCanBuses()[0], &request, 10));
    ck_assert(!diagnostics::addDiagnosticRequest(&DIAGNOSTICS_MANAGER,
            &getCanBuses()[0], &request, 11));
}
END_TEST

START_TEST (test_update_existing_recurring)
{
    ck_assert(diagnostics::addDiagnosticRequest(&DIAGNOSTICS_MANAGER,
            &getCanBuses()[0], &request, 10));
    diagnostics::sendRequests(&DIAGNOSTICS_MANAGER, &getCanBuses()[0]);
    // get around the staggered start
    FAKE_TIME += 2000;
    diagnostics::sendRequests(&DIAGNOSTICS_MANAGER, &getCanBuses()[0]);
    fail_if(canQueueEmpty(0));

    diagnostics::receiveCanMessage(&DIAGNOSTICS_MANAGER, &getCanBuses()[0],
            &message, &PIPELINE);
    fail_if(outputQueueEmpty());

    // received one response to recurring request - now reset queues

    resetQueues();

    // change request to non-recurring, which should trigger it to be sent once,
    // right now
    ck_assert(diagnostics::addDiagnosticRequest(&DIAGNOSTICS_MANAGER,
            &getCanBuses()[0], &request, 0));
    diagnostics::sendRequests(&DIAGNOSTICS_MANAGER, &getCanBuses()[0]);
    fail_if(canQueueEmpty(0));

    diagnostics::receiveCanMessage(&DIAGNOSTICS_MANAGER, &getCanBuses()[0],
            &message, &PIPELINE);
    fail_if(outputQueueEmpty());

    resetQueues();

    diagnostics::sendRequests(&DIAGNOSTICS_MANAGER, &getCanBuses()[0]);
    fail_unless(canQueueEmpty(0));
    diagnostics::receiveCanMessage(&DIAGNOSTICS_MANAGER, &getCanBuses()[0],
            &message, &PIPELINE);
    fail_unless(outputQueueEmpty());
}
END_TEST

START_TEST (test_add_basic_request)
{
    ck_assert(diagnostics::addDiagnosticRequest(&DIAGNOSTICS_MANAGER,
            &getCanBuses()[0], &request, 0));
    diagnostics::sendRequests(&DIAGNOSTICS_MANAGER, &getCanBuses()[0]);
    fail_if(canQueueEmpty(0));
    diagnostics::receiveCanMessage(&DIAGNOSTICS_MANAGER, &getCanBuses()[0],
            &message, &PIPELINE);
    fail_if(outputQueueEmpty());

    uint8_t snapshot[QUEUE_LENGTH(uint8_t, OUTPUT_QUEUE) + 1];
    QUEUE_SNAPSHOT(uint8_t, OUTPUT_QUEUE, snapshot, sizeof(snapshot));
    snapshot[sizeof(snapshot) - 1] = NULL;
    ck_assert_str_eq((char*)snapshot, "{\"bus\":1,\"id\":2016,\"mode\":1,\"success\":true,\"pid\":2,\"payload\":\"0x45\"}\r\n");
}
END_TEST

START_TEST (test_padding_on_by_default)
{
    ck_assert(diagnostics::addDiagnosticRequest(&DIAGNOSTICS_MANAGER,
                &getCanBuses()[0], &request, 0));
    diagnostics::sendRequests(&DIAGNOSTICS_MANAGER, &getCanBuses()[0]);
    fail_if(canQueueEmpty(0));
    CanMessage message = QUEUE_POP(CanMessage, &getCanBuses()[0].sendQueue);
    ck_assert_int_eq(message.length, 8);
}
END_TEST

START_TEST (test_padding_enabled)
{
    request.no_frame_padding = false;
    ck_assert(diagnostics::addDiagnosticRequest(&DIAGNOSTICS_MANAGER,
                &getCanBuses()[0], &request, 0));
    diagnostics::sendRequests(&DIAGNOSTICS_MANAGER, &getCanBuses()[0]);
    fail_if(canQueueEmpty(0));
    CanMessage message = QUEUE_POP(CanMessage, &getCanBuses()[0].sendQueue);
    ck_assert_int_eq(message.length, 8);
}
END_TEST

START_TEST (test_padding_disabled)
{
    request.no_frame_padding = true;
    ck_assert(diagnostics::addDiagnosticRequest(&DIAGNOSTICS_MANAGER,
                &getCanBuses()[0], &request, 0));
    diagnostics::sendRequests(&DIAGNOSTICS_MANAGER, &getCanBuses()[0]);
    fail_if(canQueueEmpty(0));
    CanMessage message = QUEUE_POP(CanMessage, &getCanBuses()[0].sendQueue);
    ck_assert_int_eq(message.length, 3);
}
END_TEST

START_TEST (test_add_request_other_bus)
{
    ck_assert(diagnostics::addDiagnosticRequest(&DIAGNOSTICS_MANAGER,
                &getCanBuses()[1], &request, "mypid", 1, 0, NULL, 0));
    diagnostics::sendRequests(&DIAGNOSTICS_MANAGER, &getCanBuses()[1]);
    fail_if(canQueueEmpty(1));
    diagnostics::receiveCanMessage(&DIAGNOSTICS_MANAGER, &getCanBuses()[1],
            &message, &PIPELINE);
    fail_if(outputQueueEmpty());

    uint8_t snapshot[QUEUE_LENGTH(uint8_t, OUTPUT_QUEUE) + 1];
    QUEUE_SNAPSHOT(uint8_t, OUTPUT_QUEUE, snapshot, sizeof(snapshot));
    snapshot[sizeof(snapshot) - 1] = NULL;
    ck_assert_str_eq((char*)snapshot, "{\"name\":\"mypid\",\"value\":69}\r\n");
}
END_TEST

START_TEST (test_add_request_with_name)
{
    ck_assert(diagnostics::addDiagnosticRequest(&DIAGNOSTICS_MANAGER,
            &getCanBuses()[0], &request, "mypid", 1, 0, NULL, 0));
    diagnostics::sendRequests(&DIAGNOSTICS_MANAGER, &getCanBuses()[0]);
    fail_if(canQueueEmpty(0));
    diagnostics::receiveCanMessage(&DIAGNOSTICS_MANAGER, &getCanBuses()[0],
            &message, &PIPELINE);
    fail_if(outputQueueEmpty());

    uint8_t snapshot[QUEUE_LENGTH(uint8_t, OUTPUT_QUEUE) + 1];
    QUEUE_SNAPSHOT(uint8_t, OUTPUT_QUEUE, snapshot, sizeof(snapshot));
    snapshot[sizeof(snapshot) - 1] = NULL;
    ck_assert_str_eq((char*)snapshot, "{\"name\":\"mypid\",\"value\":69}\r\n");
}
END_TEST

START_TEST (test_scaling)
{
    ck_assert(diagnostics::addDiagnosticRequest(&DIAGNOSTICS_MANAGER,
            &getCanBuses()[0], &request, "mypid", 2.0, 14, NULL, 0));
    diagnostics::sendRequests(&DIAGNOSTICS_MANAGER, &getCanBuses()[0]);
    fail_if(canQueueEmpty(0));
    diagnostics::receiveCanMessage(&DIAGNOSTICS_MANAGER, &getCanBuses()[0],
            &message, &PIPELINE);
    fail_if(outputQueueEmpty());

    uint8_t snapshot[QUEUE_LENGTH(uint8_t, OUTPUT_QUEUE) + 1];
    QUEUE_SNAPSHOT(uint8_t, OUTPUT_QUEUE, snapshot, sizeof(snapshot));
    snapshot[sizeof(snapshot) - 1] = NULL;
    ck_assert_str_eq((char*)snapshot, "{\"name\":\"mypid\",\"value\":152}\r\n");
}
END_TEST

static float decodeFloatTimes2(const DiagnosticResponse* response,
        float parsed_payload) {
    return parsed_payload * 2;
}

START_TEST (test_add_request_with_decoder_no_name_allowed)
{
    fail_unless(diagnostics::addDiagnosticRequest(&DIAGNOSTICS_MANAGER,
            &getCanBuses()[0], &request, NULL, 1, 0, decodeFloatTimes2, 0));
}
END_TEST

START_TEST (test_add_request_with_name_and_decoder)
{
    fail_unless(diagnostics::addDiagnosticRequest(&DIAGNOSTICS_MANAGER,
            &getCanBuses()[0], &request, "mypid", 1, 0, decodeFloatTimes2, 0));
    diagnostics::sendRequests(&DIAGNOSTICS_MANAGER, &getCanBuses()[0]);
    fail_if(canQueueEmpty(0));
    diagnostics::receiveCanMessage(&DIAGNOSTICS_MANAGER, &getCanBuses()[0],
            &message, &PIPELINE);
    fail_if(outputQueueEmpty());

    uint8_t snapshot[QUEUE_LENGTH(uint8_t, OUTPUT_QUEUE) + 1];
    QUEUE_SNAPSHOT(uint8_t, OUTPUT_QUEUE, snapshot, sizeof(snapshot));
    snapshot[sizeof(snapshot) - 1] = NULL;
    ck_assert_str_eq((char*)snapshot, "{\"name\":\"mypid\",\"value\":138}\r\n");
}
END_TEST

START_TEST (test_add_recurring)
{
    ck_assert(diagnostics::addDiagnosticRequest(&DIAGNOSTICS_MANAGER,
            &getCanBuses()[0], &request, 1));
    diagnostics::sendRequests(&DIAGNOSTICS_MANAGER, &getCanBuses()[0]);
    // get around the staggered start
    FAKE_TIME += 2000;
    diagnostics::sendRequests(&DIAGNOSTICS_MANAGER, &getCanBuses()[0]);
    fail_if(canQueueEmpty(0));

    diagnostics::receiveCanMessage(&DIAGNOSTICS_MANAGER, &getCanBuses()[0],
            &message, &PIPELINE);
    fail_if(outputQueueEmpty());

    resetQueues();

    diagnostics::sendRequests(&DIAGNOSTICS_MANAGER, &getCanBuses()[0]);
    fail_unless(canQueueEmpty(0));

    FAKE_TIME += 900;
    diagnostics::sendRequests(&DIAGNOSTICS_MANAGER, &getCanBuses()[0]);
    fail_unless(canQueueEmpty(0));
    FAKE_TIME += 100;
    diagnostics::sendRequests(&DIAGNOSTICS_MANAGER, &getCanBuses()[0]);
    fail_if(canQueueEmpty(0));

    diagnostics::receiveCanMessage(&DIAGNOSTICS_MANAGER, &getCanBuses()[0],
            &message, &PIPELINE);
    fail_if(outputQueueEmpty());
}
END_TEST

START_TEST (test_receive_nonrecurring_twice)
{
    ck_assert(diagnostics::addDiagnosticRequest(&DIAGNOSTICS_MANAGER,
            &getCanBuses()[0], &request, 0));
    diagnostics::sendRequests(&DIAGNOSTICS_MANAGER, &getCanBuses()[0]);
    fail_if(canQueueEmpty(0));

    diagnostics::receiveCanMessage(&DIAGNOSTICS_MANAGER, &getCanBuses()[0],
            &message, &PIPELINE);
    fail_if(outputQueueEmpty());

    // the request should be moved from inflight to active at this point

    resetQueues();

    // the non-recurring request should already be completed, so this should
    // *not* send it again
    diagnostics::sendRequests(&DIAGNOSTICS_MANAGER, &getCanBuses()[0]);
    fail_unless(canQueueEmpty(0));
    diagnostics::receiveCanMessage(&DIAGNOSTICS_MANAGER, &getCanBuses()[0],
            &message, &PIPELINE);
    fail_unless(outputQueueEmpty());
}
END_TEST

START_TEST (test_nonrecurring_timeout)
{
    ck_assert(diagnostics::addDiagnosticRequest(&DIAGNOSTICS_MANAGER,
            &getCanBuses()[0], &request, 0));
    diagnostics::sendRequests(&DIAGNOSTICS_MANAGER, &getCanBuses()[0]);
    fail_if(canQueueEmpty(0));

    resetQueues();

    diagnostics::sendRequests(&DIAGNOSTICS_MANAGER, &getCanBuses()[0]);
    fail_unless(canQueueEmpty(0));

    FAKE_TIME += 500;

    // the request timed out and it's non-recurring, so it should *not* be sent
    // again
    diagnostics::sendRequests(&DIAGNOSTICS_MANAGER, &getCanBuses()[0]);
    fail_unless(canQueueEmpty(0));
}
END_TEST

Suite* suite(void) {
    Suite* s = suite_create("diagnostics");
    TCase *tc_core = tcase_create("core");
    tcase_add_checked_fixture(tc_core, setup, NULL);
    tcase_add_test(tc_core, test_add_basic_request);
    tcase_add_test(tc_core, test_add_request_other_bus);
    tcase_add_test(tc_core, test_add_request_with_name);
    tcase_add_test(tc_core, test_add_request_with_decoder_no_name_allowed);
    tcase_add_test(tc_core, test_add_request_with_name_and_decoder);
    tcase_add_test(tc_core, test_add_recurring);
    tcase_add_test(tc_core, test_add_recurring_too_frequent);
    tcase_add_test(tc_core, test_padding_on_by_default);
    tcase_add_test(tc_core, test_padding_enabled);
    tcase_add_test(tc_core, test_padding_disabled);
    tcase_add_test(tc_core, test_scaling);
    tcase_add_test(tc_core, test_update_existing_recurring);
    tcase_add_test(tc_core, test_receive_nonrecurring_twice);
    tcase_add_test(tc_core, test_nonrecurring_timeout);

    suite_add_tcase(s, tc_core);

    return s;
}

int main(void) {
    int numberFailed;
    Suite* s = suite();
    SRunner *sr = srunner_create(s);
    // Don't fork so we can actually use gdb
    srunner_set_fork_status(sr, CK_NOFORK);
    srunner_run_all(sr, CK_NORMAL);
    numberFailed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (numberFailed == 0) ? 0 : 1;
}
