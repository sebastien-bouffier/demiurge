#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include <functional>
#include <memory>

// Sends one Messages API request at a time on a background thread and calls back on the
// message thread with the HTTP status and body.
class AiClient : private juce::Thread
{
public:
    using Callback = std::function<void (int status, const juce::String& body)>;

    AiClient();
    ~AiClient() override;

    // Returns false if a request is already running.
    bool send (const juce::var& body, const juce::String& apiKey, Callback done);
    void cancel();
    bool isBusy() const { return busy.load(); }

    static juce::String endpoint() { return "https://api.anthropic.com/v1/messages"; }

private:
    void run() override;

    juce::CriticalSection lock;
    juce::String pendingBody, pendingKey;
    Callback pendingDone;
    juce::WaitableEvent wake;
    std::atomic<bool> busy { false }, cancelled { false };
    juce::WebInputStream* currentStream = nullptr; // guarded by lock
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
};
