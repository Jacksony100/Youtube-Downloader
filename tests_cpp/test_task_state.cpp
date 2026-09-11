#include "downloads/download_task.hpp"
#include <QtTest>

using namespace vdp;

class TaskStateTests final : public QObject {
    Q_OBJECT
private slots:
    void normalLifecycle() {
        TaskLifecycle state;
        QCOMPARE(state.state(), TaskState::Queued);
        QVERIFY(!state.finalize(TaskState::Completed));
        QVERIFY(state.started());
        QCOMPARE(state.state(), TaskState::Downloading);
        QVERIFY(state.postProcessing());
        QVERIFY(state.finish(0));
        QCOMPARE(state.state(), TaskState::Completed);
        QVERIFY(!state.started());
        QVERIFY(!state.finish(1));
        QVERIFY(!state.requestCancellation());
    }
    void queuedCancellation() {
        TaskLifecycle state;
        QVERIFY(state.requestCancellation());
        QVERIFY(!state.started());
        QVERIFY(state.finish(-1));
        QCOMPARE(state.state(), TaskState::Cancelled);
    }
    void runningCancellationWinsOverFailure() {
        TaskLifecycle state;
        QVERIFY(state.started());
        QVERIFY(state.requestCancellation());
        QVERIFY(state.finish(-1, false));
        QCOMPARE(state.state(), TaskState::Cancelled);
        QVERIFY(!state.finalize(TaskState::Failed));
    }
    void processFailureAndDuplicateSignals() {
        TaskLifecycle state;
        QVERIFY(state.started());
        QVERIFY(state.finish(-1, false));
        QCOMPARE(state.state(), TaskState::Failed);
        QVERIFY(!state.finish(0));
        QVERIFY(!state.finalize(TaskState::Failed));
        TaskLifecycle retry;
        QVERIFY(retry.started());
        QVERIFY(retry.finish(0));
        QCOMPARE(retry.state(), TaskState::Completed);
    }
    void failedStart() {
        TaskLifecycle state;
        QVERIFY(state.finalize(TaskState::Failed));
        QVERIFY(!state.started());
        QCOMPARE(state.state(), TaskState::Failed);
    }
    void stateKeysRoundTrip() {
        for (auto state : {TaskState::Queued, TaskState::Preparing, TaskState::Downloading,
                 TaskState::PostProcessing, TaskState::Completed, TaskState::Cancelled, TaskState::Failed}) {
            QVERIFY(taskStateFromKey(taskStateKey(state)));
            QCOMPARE(*taskStateFromKey(taskStateKey(state)), state);
            QVERIFY(!taskStateLabel(state).isEmpty());
        }
        QVERIFY(!taskStateFromKey("invalid"));
    }
};

QTEST_GUILESS_MAIN(TaskStateTests)
#include "test_task_state.moc"
