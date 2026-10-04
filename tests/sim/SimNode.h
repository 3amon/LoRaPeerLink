/**
 * @file SimNode.h
 * @brief A complete simulated node: radio, link, RollCall and PeerMessenger.
 */
#ifndef SIM_NODE_H
#define SIM_NODE_H

#include "EncryptedLoRaLink.h"
#include "PeerMessenger.h"
#include "RollCall.h"
#include "sim/SimRadio.h"

#include <algorithm>
#include <string>
#include <vector>

namespace simtest {

/**
 * The stack an application would build, on top of a simulated radio.
 * LinkT is LoRaBasicLink or LoRaBackoffLink. Nodes are not movable (the
 * layers hold pointers to each other), so keep them in unique_ptrs.
 */
template <class LinkT>
struct Node {
    sim::SimRadio& radio;
    LinkT link;
    RollCall rollCall;
    PeerMessenger messenger;
    std::vector<UserMessage> inbox;     ///< Everything pump() has read from the messenger

    Node(sim::World& world, const std::string& name, sim::RxMode mode,
         RollCall::random_fn random = sim::rand16)
        : radio(world.addRadio(mode)),
          link(&radio, sim::nowMs, sim::sleepMs),
          rollCall(&link, name, sim::nowMs, sim::sleepMs, random),
          messenger(&rollCall) {}

    Node(const Node&) = delete;
    Node& operator=(const Node&) = delete;

    /** Move received messages from the messenger's queue to inbox. */
    void drain() {
        while (messenger.hasMessage()) inbox.push_back(messenger.receiveMessage());
    }

    /** Run the application main loop until virtual time @p untilMs. */
    void pump(uint32_t untilMs, uint32_t windowMs = 500) {
        while (sim::nowMs() < untilMs) {
            messenger.processMessages(std::min(windowMs, untilMs - sim::nowMs()));
            drain();
        }
    }

    int countContent(const std::string& content) const {
        return static_cast<int>(std::count_if(inbox.begin(), inbox.end(),
                                              [&](const UserMessage& m) { return m.content == content; }));
    }
};

/** Same as Node, with an EncryptedLoRaLink between the link and RollCall. */
template <class LinkT>
struct EncNode {
    sim::SimRadio& radio;
    LinkT link;
    EncryptedLoRaLink secure;
    RollCall rollCall;
    PeerMessenger messenger;
    std::vector<UserMessage> inbox;

    EncNode(sim::World& world, const std::string& name, sim::RxMode mode,
            const std::string& network, const std::string& password, uint32_t iterations = 64)
        : radio(world.addRadio(mode)),
          link(&radio, sim::nowMs, sim::sleepMs),
          secure(&link, network, password, iterations, sim::nowMs),
          rollCall(&secure, name, sim::nowMs, sim::sleepMs, sim::rand16),
          messenger(&rollCall) {}

    EncNode(const EncNode&) = delete;
    EncNode& operator=(const EncNode&) = delete;

    void drain() {
        while (messenger.hasMessage()) inbox.push_back(messenger.receiveMessage());
    }

    void pump(uint32_t untilMs, uint32_t windowMs = 500) {
        while (sim::nowMs() < untilMs) {
            messenger.processMessages(std::min(windowMs, untilMs - sim::nowMs()));
            drain();
        }
    }

    int countContent(const std::string& content) const {
        return static_cast<int>(std::count_if(inbox.begin(), inbox.end(),
                                              [&](const UserMessage& m) { return m.content == content; }));
    }
};

} // namespace simtest

#endif // SIM_NODE_H
