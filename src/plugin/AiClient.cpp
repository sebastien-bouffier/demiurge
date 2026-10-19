#include "AiClient.h"

#include "engine/Composer.h"

AiClient::AiClient() : juce::Thread ("Demiurge AI")
{
    startThread();
}

AiClient::~AiClient()
{
    alive->store (false);
    cancel();
    signalThreadShouldExit();
    wake.signal();
    stopThread (4000);
}

bool AiClient::send (const juce::var& body, const juce::String& apiKey, Callback done)
{
    if (busy.exchange (true))
        return false;

    {
        const juce::ScopedLock sl (lock);
        pendingBody = juce::JSON::toString (body, true);
        pendingKey = apiKey;
        pendingDone = std::move (done);
        cancelled = false;
    }
    wake.signal();
    return true;
}

void AiClient::cancel()
{
    cancelled = true;
    const juce::ScopedLock sl (lock);
    if (currentStream != nullptr)
        currentStream->cancel();
}

void AiClient::run()
{
    while (! threadShouldExit())
    {
        wake.wait (-1);
        if (threadShouldExit())
            break;

        juce::String body, key;
        Callback done;
        {
            const juce::ScopedLock sl (lock);
            body = pendingBody;
            key = pendingKey;
            done = std::move (pendingDone);
            pendingDone = nullptr;
        }
        if (done == nullptr)
            continue;

        int status = 0;
        juce::String response;
        received = 0;

        const auto url = juce::URL (endpoint()).withPOSTData (body);
        juce::String headers;
        headers << "Content-Type: application/json\r\n"
                << "x-api-key: " << key << "\r\n"
                << "anthropic-version: 2023-06-01\r\n";
        if (body.contains ("\"fallbacks\""))
            headers << "anthropic-beta: server-side-fallback-2026-07-01\r\n";

        // Overloaded or rate limited: wait and try again a few times.
        for (int attempt = 0;; ++attempt)
        {
            int retryAfter = 0;
            {
                juce::WebInputStream stream (url, true);
                stream.withExtraHeaders (headers).withConnectionTimeout (10 * 60 * 1000).withNumRedirectsToFollow (2);
                {
                    const juce::ScopedLock sl (lock);
                    currentStream = &stream;
                }

                if (! cancelled && stream.connect (nullptr))
                {
                    status = stream.getStatusCode();
                    retryAfter = stream.getResponseHeaders()["retry-after"].getIntValue();
                    const auto type = stream.getResponseHeaders()["Content-Type"];
                    if (status == 200 && type.containsIgnoreCase ("text/event-stream"))
                    {
                        dm::composer::StreamAccumulator events;
                        while (! cancelled && ! threadShouldExit() && ! stream.isExhausted())
                        {
                            events.addLine (stream.readNextLine().trimEnd());
                            received = events.textLength();
                        }
                        response = events.message();
                    }
                    else
                    {
                        response = stream.readEntireStreamAsString();
                    }
                }
                else if (! cancelled)
                {
                    status = stream.getStatusCode();
                    response = stream.readEntireStreamAsString();
                    if (response.isEmpty())
                        response = "{\"type\":\"error\",\"error\":{\"message\":\"Could not reach api.anthropic.com. "
                                   "Check the internet connection.\"}}";
                }

                const juce::ScopedLock sl (lock);
                currentStream = nullptr;
            }

            const bool busyServer = status == 429 || status == 529 || response.contains ("overloaded_error");
            if (! busyServer || attempt >= 3 || cancelled || threadShouldExit())
                break;
            const int waitMs = 1000 * juce::jlimit (2, 60, retryAfter > 0 ? retryAfter : 5 << attempt);
            for (int t = 0; t < waitMs && ! cancelled && ! threadShouldExit(); t += 100)
                juce::Thread::sleep (100);
        }

        if (cancelled)
        {
            status = -1;
            response = "{\"type\":\"error\",\"error\":{\"message\":\"Cancelled.\"}}";
        }

        busy = false;
        auto flag = alive;
        juce::MessageManager::callAsync ([flag, done, status, response]
        {
            if (flag->load())
                done (status, response);
        });
    }
}
