#define CATCH_CONFIG_MAIN
#include <ks/thirdparty/catch/catch.hpp>

#include <ks/KsGlobal.h>
#include <ks/KsObject.h>
#include <ks/KsApplication.h>
#include <ks/KsTimer.h>

using namespace ks;

// ============================================================= //
// ============================================================= //

void CountThenReturn(uint * count)
{
    (*count) += 1;
}

// ============================================================= //

TEST_CASE("EventLoop","[evloop]")
{
    uint count = 0;
    std::chrono::milliseconds sleep_ms(10);
    auto count_then_ret = std::bind(CountThenReturn,&count);

    shared_ptr<EventLoop> event_loop = make_shared<EventLoop>();

    SECTION("PostEvents")
    {
        event_loop->PostEvent(make_unique<SlotEvent>(count_then_ret));
        event_loop->PostEvent(make_unique<SlotEvent>(count_then_ret));
        event_loop->PostEvent(make_unique<SlotEvent>(count_then_ret));

        SECTION(" ~ ")
        {
            event_loop.reset(); // Stop
            REQUIRE(count==0);
        }

        SECTION("Stop, Wait")
        {
            event_loop->Stop();
            event_loop->Wait(); // Should do nothing as the
            REQUIRE(count==0);  // event loop was never started
        }

        SECTION("PostStopEvent, Wait")
        {
            event_loop->PostStopEvent();
            event_loop->Wait(); // Should do nothing as the
            REQUIRE(count==0);  // event loop was never started
        }

        SECTION("ProcessEvents, Stop, Wait")
        {
            event_loop->ProcessEvents();    // Should do nothing as the
            event_loop->Stop();             // event loop was never started
            event_loop->Wait();
            REQUIRE(count==0);
        }

        SECTION("Start, ProcessEvents")
        {
            event_loop->Start();
            event_loop->ProcessEvents();

            SECTION(" ~ ")
            {
                event_loop.reset();
                REQUIRE(count==3);
            }

            SECTION("Start")
            {
                // Expect multiple start calls to
                // do nothing to queued events
                event_loop->PostEvent(make_unique<SlotEvent>(count_then_ret));
                event_loop->PostEvent(make_unique<SlotEvent>(count_then_ret));
                event_loop->Start();
                REQUIRE(count==3);
            }

            SECTION("Stop","Wait")
            {
                event_loop->Stop();
                event_loop->Wait();
                REQUIRE(count==3);

                SECTION("Start, PostEvents, ProcessEvents, Stop, Wait")
                {
                    // restart
                    event_loop->Start();
                    event_loop->PostEvent(make_unique<SlotEvent>(count_then_ret));
                    event_loop->PostEvent(make_unique<SlotEvent>(count_then_ret));
                    event_loop->ProcessEvents();
                    event_loop->Stop();
                    event_loop->Wait();

                    REQUIRE(count==5);
                }
            }
        }

        SECTION("Start, ProcessEvents (thread)")
        {
            event_loop->Start();
            event_loop->ProcessEvents();

            // Ensure that calling ProcessEvents from a second
            // thread without stopping the event loop is an error
            bool ok = false;
            std::thread thread(
                        [&ok,event_loop]
                        () {
                            ok = event_loop->ProcessEvents();
                        });

            REQUIRE_FALSE(ok);
            thread.join();
        }

        SECTION("Run")
        {
            event_loop->Run();
            REQUIRE(count==0); // didn't call Start yet
        }

        SECTION("Start (thread), Run (thread)")
        {
            bool ok = false;
            std::thread thread = EventLoop::LaunchInThread(event_loop,&ok);

            SECTION("Stop")
            {
                event_loop->Stop();
                event_loop->Wait();
                thread.join();

                // count could be anything as any number of events
                // may have been processed before we call Stop

                // 'ok' could be anything as Stop in this thread might have
                // been called before Run is called in the spawned thread
            }

            SECTION("PostStopEvent,PostEvents")
            {
                event_loop->PostEvent(make_unique<SlotEvent>(count_then_ret));
                event_loop->PostEvent(make_unique<SlotEvent>(count_then_ret));
                event_loop->PostStopEvent();
                event_loop->PostEvent(make_unique<SlotEvent>(count_then_ret));
                event_loop->PostEvent(make_unique<SlotEvent>(count_then_ret));
                thread.join();
                REQUIRE(count==5);
            }

            SECTION("PostStopEvent, Wait")
            {
                event_loop->PostEvent(make_unique<SlotEvent>(count_then_ret));
                event_loop->PostEvent(make_unique<SlotEvent>(count_then_ret));
                event_loop->PostStopEvent();
                event_loop->Wait();
                REQUIRE(count==5);
                thread.join();
            }

            SECTION("PostStopEvent, Wait, Start, PostEvents, Stop")
            {
                event_loop->PostStopEvent();
                event_loop->Wait(); // should be stopped
                REQUIRE(count==3);

                event_loop->Start();
                event_loop->PostEvent(make_unique<SlotEvent>(count_then_ret));
                event_loop->PostEvent(make_unique<SlotEvent>(count_then_ret));
                event_loop->Stop();
                event_loop->Wait();

                // The event loop is stopped with the initial PostStopEvent
                // and can't be restarted without calling Run() again (since
                // we're not using ProcessEvents here, its a blocking loop
                // in another thread); thus no more events should be invoked
                REQUIRE(count==3);
                thread.join();
            }
        }
    }
}

// ============================================================= //
// ============================================================= //

class Derived0 : public Object
{
    friend class ks::ObjectBuilder;
    typedef ks::Object base_type;

public:
    ~Derived0()
    {

    }

    std::string m_create;

protected:
    Derived0() :
        Object(nullptr)
    {
        m_create += "Construct0";
    }

private:
    void init()
    {
        m_create += "Init0";
    }
};

class Derived1 : public Derived0
{
    friend class ks::ObjectBuilder;
    typedef Derived0 base_type;

public:
    ~Derived1()
    {

    }

protected:
    Derived1()
    {
        m_create += "Construct1";
    }

private:
    void init()
    {
        m_create += "Init1";
    }
};

// ============================================================= //

TEST_CASE("Objects","[objects]")
{
    // Check construction and init order for make_object
    // with inheritance chains

    std::string const expect_create =
            "Construct0Construct1Init0Init1";

    shared_ptr<Derived1> d1 = make_object<Derived1>();
    bool ok = (expect_create == d1->m_create);

    REQUIRE(ok);
}

// ============================================================= //
// ============================================================= //

class TrivialReceiver : public Object
{
    friend class ks::ObjectBuilder;
    typedef Object base_type;

protected:
    TrivialReceiver(shared_ptr<EventLoop> event_loop) :
        Object(event_loop),
        invoke_count(0)
    {
        // empty
    }

public:
    void SlotCheck(bool * ok)
    {
        (*ok) = true;
    }

    void SlotCount()
    {
        invoke_count++;
    }

    void SlotSignalSelf(uint x, EventLoop * event_loop)
    {
        shared_ptr<TrivialReceiver> this_receiver =
                std::static_pointer_cast<TrivialReceiver>(
                    shared_from_this());

        if(x > 4) {
            event_loop->Stop();
            return;
        }

        Signal<uint,EventLoop*> signal_self;
        signal_self.Connect(
                    this_receiver,
                    &TrivialReceiver::SlotSignalSelf);

        // If the slot associated with signal_self is invoked
        // after the string append (ie. its queued), the string
        // would be 01234
        signal_self.Emit(x+1,event_loop);
        misc_string.append(ks::to_string(x));
    }

    void SlotSignalSelfBlocking(uint x, EventLoop * event_loop)
    {
        shared_ptr<TrivialReceiver> this_receiver =
                std::static_pointer_cast<TrivialReceiver>(
                    shared_from_this());

        if(x > 4) {
            event_loop->Stop();
            return;
        }

        Signal<uint,EventLoop*> signal_self;
        signal_self.Connect(
                    this_receiver,
                    &TrivialReceiver::SlotSignalSelfBlocking,
                    ConnectionType::BlockingQueued);

        // If the slot associated with signal_self is invoked
        // after the string append (ie. its queued), the string
        // would be 01234
        signal_self.Emit(x+1,event_loop);
        misc_string.append(ks::to_string(x));
    }

    void SlotPrintAndCheckThreadId(std::string str,
                                   std::thread::id thread_id)
    {
        if(thread_id == std::this_thread::get_id()) {
            misc_string.append(str);
        }
    }

    void SlotThreadId()
    {
        thread_id = std::this_thread::get_id();
    }

    void SlotStopEventLoop(EventLoop * event_loop)
    {
        event_loop->Stop();
    }

    uint invoke_count;
    std::thread::id thread_id;

    std::string misc_string;

private:
    void init()
    {

    }
};

// ============================================================= //

TEST_CASE("Signals","[signals]")
{
    shared_ptr<EventLoop> event_loop = make_shared<EventLoop>();

    SECTION("Connect/Disconnect")
    {
        std::thread thread0 = EventLoop::LaunchInThread(event_loop);

        shared_ptr<TrivialReceiver> receiver =
                make_object<TrivialReceiver>(event_loop);

        Signal<bool*> signal_check;
        Signal<EventLoop*> signal_stop;

        auto cid0 = signal_check.Connect(
                    receiver,
                    &TrivialReceiver::SlotCheck);

        auto cid1 = signal_stop.Connect(
                    receiver,
                    &TrivialReceiver::SlotStopEventLoop);

        // ensure the connection succeeded by
        // testing the slots
        bool ok = false;
        signal_check.Emit(&ok);
        signal_stop.Emit(event_loop.get());
        event_loop->Wait();
        REQUIRE(ok);

        thread0.join();


        // try disconnecting
        REQUIRE(signal_check.Disconnect(cid0));

        // verify disconnection by testing the slots

        // restart event loop
        std::thread thread1 = EventLoop::LaunchInThread(event_loop);

        ok = false;
        signal_check.Emit(&ok);
        signal_stop.Emit(event_loop.get());
        event_loop->Wait();
        REQUIRE_FALSE(ok);

        // require repeat disconnects to fail
        REQUIRE_FALSE(signal_check.Disconnect(cid0));

        // require Disconnect on a non existant
        // connection id to fail
        REQUIRE_FALSE(signal_check.Disconnect(1234));

        thread1.join();
    }

    SECTION("Expired Connections")
    {
        std::thread thread = EventLoop::LaunchInThread(event_loop);

        Signal<bool*> signal_check;
        Id cid0;

        {
            shared_ptr<TrivialReceiver> temp_receiver =
                    make_object<TrivialReceiver>(event_loop);

            cid0 = signal_check.Connect(
                        temp_receiver,
                        &TrivialReceiver::SlotCheck);
        }

        // Check that the connections were
        // made successfully
        REQUIRE(signal_check.ConnectionValid(cid0));

        // Emit should remove expired connections
        bool ok;
        signal_check.Emit(&ok);
        REQUIRE_FALSE(signal_check.ConnectionValid(cid0));

        event_loop->Stop();
        thread.join();
    }

    SECTION("One-to-one and one-to-many connections")
    {
        std::thread thread0 = EventLoop::LaunchInThread(event_loop);

        // signals
        Signal<> signal_count;
        Signal<EventLoop*> signal_stop;

        // r0
        shared_ptr<TrivialReceiver> r0 =
                make_object<TrivialReceiver>(event_loop);

        size_t const one_one_count = 100;

        // test 1 signal -> 1 slot
        r0->invoke_count = 0;

        signal_count.Connect(r0,&TrivialReceiver::SlotCount);
        signal_stop.Connect(r0,&TrivialReceiver::SlotStopEventLoop);

        for(uint i=0; i < one_one_count; i++) {
            signal_count.Emit();
        }
        signal_stop.Emit(event_loop.get()); // stop event loop
        event_loop->Wait();
        REQUIRE(r0->invoke_count == one_one_count);

        thread0.join();


        // test 1 signal -> 4 slots

        // add receivers
        shared_ptr<TrivialReceiver> r1 = make_object<TrivialReceiver>(event_loop);
        shared_ptr<TrivialReceiver> r2 = make_object<TrivialReceiver>(event_loop);
        shared_ptr<TrivialReceiver> r3 = make_object<TrivialReceiver>(event_loop);

        // zero count
        r0->invoke_count = 0;
        r1->invoke_count = 0;
        r2->invoke_count = 0;
        r3->invoke_count = 0;

        size_t const one_many_count=100;

        // restart event loop
        std::thread thread1 = EventLoop::LaunchInThread(event_loop);

        signal_count.Connect(r1,&TrivialReceiver::SlotCount);
        signal_count.Connect(r2,&TrivialReceiver::SlotCount);
        signal_count.Connect(r3,&TrivialReceiver::SlotCount);

        for(uint i=0; i < one_many_count; i++) {
            signal_count.Emit();
        }
        signal_stop.Emit(event_loop.get()); // stop event loop
        event_loop->Wait();

        REQUIRE((r0->invoke_count+
                 r1->invoke_count+
                 r2->invoke_count+
                 r3->invoke_count) == one_many_count*4);

        thread1.join();
    }

    SECTION("Connection types")
    {
        std::thread thread = EventLoop::LaunchInThread(event_loop);

        shared_ptr<TrivialReceiver> receiver =
                make_object<TrivialReceiver>(event_loop);

        SECTION("Direct connection")
        {
            // Ensure that the slot is invoked directly
            // by this thread by comparing thread ids
            Signal<> signal_set_thread_id;
            signal_set_thread_id.Connect(
                        receiver,
                        &TrivialReceiver::SlotThreadId,
                        ConnectionType::Direct);

            signal_set_thread_id.Emit();
            EventLoop::RemoveFromThread(event_loop,thread,true);

            bool const check_ok =
                    (std::this_thread::get_id() == receiver->thread_id);

            REQUIRE(check_ok);
        }

        SECTION("Queued connection / Same thread")
        {
            // Same thread
            Signal<uint,EventLoop*> signal_self;
            signal_self.Connect(
                        receiver,
                        &TrivialReceiver::SlotSignalSelf);

            signal_self.Emit(0,event_loop.get());
            thread.join();

            bool const check_ok =
                    (receiver->misc_string == "01234");
            REQUIRE(check_ok);
        }

        SECTION("Queued connection / Different thread")
        {
            // Different thread

            // Test to see that
            // 1. The slots are invoked in the right order
            //    relative to how the signals were sent
            // 2. The slots were invoked by the thread that
            //    is running event_loop

            Signal<std::string,std::thread::id> signal_str_threadid;
            signal_str_threadid.Connect(
                        receiver,
                        &TrivialReceiver::SlotPrintAndCheckThreadId);

            std::thread::id const check_id = thread.get_id();
            signal_str_threadid.Emit("h",check_id);
            signal_str_threadid.Emit("e",check_id);
            signal_str_threadid.Emit("l",check_id);
            signal_str_threadid.Emit("l",check_id);
            signal_str_threadid.Emit("o",check_id);

            EventLoop::RemoveFromThread(event_loop,thread,true);

            bool const check_ok =
                    (receiver->misc_string == "hello");

            REQUIRE(check_ok);
        }

        SECTION("Blocking connection / Same thread")
        {
            // Same thread
            Signal<uint,EventLoop*> signal_self;
            signal_self.Connect(
                        receiver,
                        &TrivialReceiver::SlotSignalSelfBlocking,
                        ConnectionType::BlockingQueued);

            signal_self.Emit(0,event_loop.get());
            thread.join();

            bool const check_ok =
                    (receiver->misc_string == "43210");
            REQUIRE(check_ok);
        }

        SECTION("Blocking connection / Different thread")
        {
            Signal<> signal_count;
            signal_count.Connect(
                        receiver,
                        &TrivialReceiver::SlotCount,
                        ConnectionType::BlockingQueued);

            // Calling Emit() should block this thread until
            // receivers corresponding slot is invoked. To test
            // this, we manually increment count in lockstep
            signal_count.Emit();        // count = 1
            receiver->invoke_count++;   // count = 2
            signal_count.Emit();        // count = 3
            receiver->invoke_count++;   // count = 4
            signal_count.Emit();        // count = 5
            receiver->invoke_count++;   // count = 6

            REQUIRE(receiver->invoke_count == 6);
            EventLoop::RemoveFromThread(event_loop,thread,true);
        }
    }
}


// ============================================================= //
// ============================================================= //

class WakeupReceiver : public Object
{
    friend class ks::ObjectBuilder;
    typedef Object base_type;

public:
    ~WakeupReceiver()
    {

    }

    void Prepare(uint wakeup_limit)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_waiting = true;
        m_wakeup_count = 0;
        m_wakeup_limit = wakeup_limit;
    }

    void Block()
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        while(m_waiting) {
            m_cv.wait(lock);
        }
    }

    void OnSleepFor(std::chrono::milliseconds sleep_ms)
    {
        std::this_thread::sleep_for(sleep_ms);
    }

    void OnWakeup()
    {
        m_wakeup_count++;

        if(m_wakeup_count < m_wakeup_limit) {
            return;
        }

        std::lock_guard<std::mutex> lock(m_mutex);
        m_waiting = false;
        m_cv.notify_all();
    }

protected:
    WakeupReceiver(shared_ptr<EventLoop> event_loop) :
        Object(event_loop),
        m_wakeup_count(0),
        m_wakeup_limit(0),
        m_waiting(false)
    {
       // empty
    }

private:
    void init()
    {

    }

    uint m_wakeup_count;
    uint m_wakeup_limit;
    bool m_waiting;
    std::mutex m_mutex;
    std::condition_variable m_cv;
};

// ============================================================= //

TEST_CASE("ks::Timer","[timers]") {

    SECTION("inactive tests: ") {
        shared_ptr<EventLoop> event_loop =
                make_shared<EventLoop>();

        shared_ptr<Timer> timer =
                make_object<Timer>(event_loop);

        SECTION("destroy inactive") {
            shared_ptr<Timer> timer = nullptr;
        }

        SECTION("stop inactive") {
            timer->Stop();
            shared_ptr<Timer> timer = nullptr;
        }

        SECTION("start/stop fuzz inactive") {
            timer->Start(std::chrono::milliseconds(10),false);
            timer->Start(std::chrono::milliseconds(10),false);
            timer->Stop();
            timer->Stop();
            timer->Start(std::chrono::milliseconds(10),false);
            timer->Stop();
            timer->Start(std::chrono::milliseconds(10),false);
            timer->Stop();
            shared_ptr<Timer> timer = nullptr;
        }
    }

    SECTION("single timer: ") {
        shared_ptr<EventLoop> event_loop =
                make_shared<EventLoop>();

        std::thread thread = EventLoop::LaunchInThread(event_loop);

        shared_ptr<Timer> timer =
                make_object<Timer>(event_loop);

        shared_ptr<WakeupReceiver> receiver =
                make_object<WakeupReceiver>(event_loop);

        timer->SignalTimeout.Connect(
                    receiver,
                    &WakeupReceiver::OnWakeup);

        SECTION("single shot / sequential: ") {
            // single shot
            std::chrono::time_point<std::chrono::steady_clock> start,end;
            start = std::chrono::steady_clock::now();

            timer->Start(std::chrono::milliseconds(50),false);
            receiver->Prepare(1); // wait for 1 timeout signal
            receiver->Block();

            end = std::chrono::steady_clock::now();
            std::chrono::milliseconds interval_ms =
                    std::chrono::duration_cast<
                        std::chrono::milliseconds
                    >(end-start);

            // The single shot timer should have marked the
            // timer inactive after timing out
            REQUIRE_FALSE(timer->GetActive());

            // The interval must be greater than the requested amount
            REQUIRE(interval_ms.count() >= 50);


            // sequential

            // Sequential calls to Start cancel the previous
            // timer and should almost always prevent it from
            // emitting a timeout signal. We provide a reasonable
            // interval (50+ms) so that subsequent calls cancel
            // the timer and expect only one timeout (the last one)
            start = std::chrono::steady_clock::now();

            receiver->Prepare(1); // wait for 1 timeout signal
            timer->Start(std::chrono::milliseconds(50),false);
            timer->Start(std::chrono::milliseconds(60),false);
            timer->Start(std::chrono::milliseconds(70),false);
            receiver->Block();

            end = std::chrono::steady_clock::now();
            interval_ms =
                    std::chrono::duration_cast<
                        std::chrono::milliseconds
                    >(end-start);

            // The single shot timer should have marked the
            // timer inactive after timing out
            REQUIRE_FALSE(timer->GetActive());

            // The interval must be greater than the requested amount
            REQUIRE(interval_ms.count() >= 70);

            // Cleanup
            event_loop->Stop();
            event_loop->Wait();
            thread.join();
        }

        SECTION("repeating: ") {
            std::chrono::time_point<std::chrono::steady_clock> start,end;
            start = std::chrono::steady_clock::now();

            receiver->Prepare(3); // wait for 3 timeout signals
            timer->Start(std::chrono::milliseconds(33),true);
            receiver->Block();

            end = std::chrono::steady_clock::now();
            std::chrono::milliseconds interval_ms =
                    std::chrono::duration_cast<
                        std::chrono::milliseconds
                    >(end-start);

            // repeat timer should stay active until
            // reset or destroyed
            REQUIRE(timer->GetActive());

            // The interval must be greater than the requested amount
            REQUIRE(interval_ms.count() >= 99);

            // Cleanup
            event_loop->Stop();
            event_loop->Wait();
            thread.join();
        }

        SECTION("delayed start: ") {
            // We want to ensure that timers are started
            // immediately by the calling thread and not
            // delayed by posting their start as an event
            // in the actual event loop.

            // For example if there are two events:
            // [busy event 25ms] [timer start]

            // The 'busy event' that takes 25ms to complete
            // should not delay the start of the timer

            // emit a signal to put the receiver's event loop's
            // thread to sleep
            Signal<std::chrono::milliseconds> SignalSleepFor;
            SignalSleepFor.Connect(
                        receiver,
                        &WakeupReceiver::OnSleepFor);

            std::chrono::time_point<std::chrono::steady_clock> start,end;
            start = std::chrono::steady_clock::now();

            SignalSleepFor.Emit(std::chrono::milliseconds(25));
            timer->Start(std::chrono::milliseconds(25),false);
            receiver->Prepare(1);
            receiver->Block();

            end = std::chrono::steady_clock::now();
            std::chrono::milliseconds interval_ms =
                    std::chrono::duration_cast<
                        std::chrono::milliseconds
                    >(end-start);

            // Cleanup
            event_loop->Stop();
            event_loop->Wait();
            thread.join();

            // The single shot timer should have marked the
            // timer inactive after timing out
            REQUIRE_FALSE(timer->GetActive());

            // The interval shouldn't have been affected
            // by the delay so it should be about 25ms
            bool ok = (interval_ms.count() >= 25) && (interval_ms.count() <= 30);
            REQUIRE(ok);
        }
    }
}

// ============================================================= //
// ============================================================= //

class TrivialApplication : public Application
{
    friend class ks::ObjectBuilder;
    typedef Application base_type;

protected:
    TrivialApplication() :
        m_ret_val(0),
        m_keep_running(false)
    {

    }

public:
    ~TrivialApplication()
    {

    }

    sint Run()
    {
        this->GetEventLoop()->Start();
        m_keep_running = true;

        while(m_keep_running) {
            this->GetEventLoop()->ProcessEvents();

            std::this_thread::sleep_for(
                std::chrono::milliseconds(16));
        }

        return m_ret_val;
    }

private:
    void quit(sint ret_val)
    {
        m_keep_running = false;
        this->GetEventLoop()->Stop();
        m_ret_val = ret_val;
    }

    void init()
    {

    }

    sint m_ret_val;
    bool m_keep_running;
};

// ============================================================= //

class CleanupObject : public Object
{
    friend class ks::ObjectBuilder;
    typedef Object base_type;

protected:
    CleanupObject(shared_ptr<EventLoop> event_loop,
                  std::atomic<uint> * i) :
        Object(event_loop),
        m_i(i)
    {

    }

public:
    ~CleanupObject()
    {

    }

    void OnCleanup()
    {
        // clean up some stuff
        std::atomic<uint> &i = (*m_i);
        i--;

        SignalFinishedCleanup.Emit(this->GetId());
    }

    Signal<Id> SignalFinishedCleanup;

private:
    void init()
    {

    }

    std::atomic<uint> * m_i;
};

// ============================================================= //

TEST_CASE("ks::Application","[application]") {

    shared_ptr<Application> app =
            make_object<TrivialApplication>();

    SECTION("Test cleanup") {

        std::atomic<uint> i(4);

        // cleanup objects in app event_loop
        shared_ptr<CleanupObject> r0 =
                make_object<CleanupObject>(app->GetEventLoop(),&i);

        shared_ptr<CleanupObject> r1 =
                make_object<CleanupObject>(app->GetEventLoop(),&i);

        // cleanup objects in alt event_loop
        shared_ptr<EventLoop> event_loop = make_shared<EventLoop>();
        std::thread thread = EventLoop::LaunchInThread(event_loop);

        shared_ptr<CleanupObject> r2 =
                make_object<CleanupObject>(event_loop,&i);

        shared_ptr<CleanupObject> r3 =
                make_object<CleanupObject>(event_loop,&i);

        app->AddCleanupRequest(r0);
        app->AddCleanupRequest(r1);
        app->AddCleanupRequest(r2);
        app->AddCleanupRequest(r3);

        // connect
        app->SignalStartCleanup.Connect(
                    r0,&CleanupObject::OnCleanup);

        app->SignalStartCleanup.Connect(
                    r1,&CleanupObject::OnCleanup);

        app->SignalStartCleanup.Connect(
                    r2,&CleanupObject::OnCleanup);

        app->SignalStartCleanup.Connect(
                    r3,&CleanupObject::OnCleanup);


        r0->SignalFinishedCleanup.Connect(
                    app,&Application::OnFinishedCleanup);

        r1->SignalFinishedCleanup.Connect(
                    app,&Application::OnFinishedCleanup);

        r2->SignalFinishedCleanup.Connect(
                    app,&Application::OnFinishedCleanup);

        r3->SignalFinishedCleanup.Connect(
                    app,&Application::OnFinishedCleanup);

        app->SignalStartCleanup.Emit();
        app->Run();

        EventLoop::RemoveFromThread(event_loop,thread);

        REQUIRE(i==0);
    }
}
