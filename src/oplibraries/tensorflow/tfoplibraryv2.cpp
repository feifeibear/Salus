/*
 * <one line to give the library's name and an idea of what it does.>
 * Copyright (C) 2017  Aetf <aetf@unlimitedcodeworks.xyz>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Make sure tensorflow_headers is included first before
 * any other headers, so we can correctly override TF logging
 * with ours.
 */
#include "oplibraries/tensorflow/tensorflow_headers.h"

#include "oplibraries/tensorflow/tfoplibraryv2.h"

#include "oplibraries/tensorflow/handlercallback.h"
#include "oplibraries/tensorflow/tfexception.h"
#include "oplibraries/tensorflow/tfinstance.h"
#include "oplibraries/tensorflow/tfsession.h"

namespace zrpc = executor;

namespace salus::oplib::tensorflow {

namespace {

template<typename REQUEST>
auto prepareTFCall(const zrpc::CustomRequest &creq);

#define IMPL_PARSE(name)                                                                                               \
    template<>                                                                                                         \
    auto prepareTFCall<tf::name##Request>(const zrpc::CustomRequest &creq)                                             \
    {                                                                                                                  \
        auto tfreq = sstl::createMessage<tf::name##Request>("tensorflow." #name "Request", creq.extra().data(),        \
                                                            creq.extra().size());                                      \
        if (!tfreq) {                                                                                                  \
            throw TFException(                                                                                         \
                tf::errors::InvalidArgument("Failed to parse message as", "tensorflow." #name "Request"));             \
        }                                                                                                              \
                                                                                                                       \
        return std::make_pair(std::move(tfreq), std::make_unique<tf::name##Response>());                               \
    }

CallWithMasterMethodName(IMPL_PARSE)

#undef IMPL_PARSE

    OpLibraryRegistary::Register tfoplibraryv2(executor::TENSORFLOW, std::make_unique<TFOpLibraryV2>(), 200);

} // namespace

bool TFOpLibraryV2::initialize()
{
    return true;
}

void TFOpLibraryV2::uninitialize()
{
    VLOG(2) << "TFOpLibraryV2 unloaded.";
}

bool TFOpLibraryV2::accepts(const zrpc::OpKernelDef &operation)
{
    return operation.oplibrary() == zrpc::TENSORFLOW;
}

void TFOpLibraryV2::onRunGraph(ZmqServer::Sender sender, const zrpc::EvenlopDef &evenlop,
                               const zrpc::RunGraphRequest &request, DoneCallback cb)
{
    UNUSED(sender);
    UNUSED(evenlop);
    UNUSED(request);

    cb(nullptr);
}

void TFOpLibraryV2::onRun(ZmqServer::Sender sender, const zrpc::EvenlopDef &evenlop, const zrpc::RunRequest &request,
                          DoneCallback cb)
{
    UNUSED(sender);
    UNUSED(evenlop);
    UNUSED(request);

    cb(nullptr);
}

void TFOpLibraryV2::onCustom(ZmqServer::Sender sender, const zrpc::EvenlopDef &evenlop, const zrpc::CustomRequest &creq,
                             DoneCallback cb)
{
    UNUSED(sender);

    using Method = std::function<void(const zrpc::CustomRequest &, HandlerCallback &&)>;
    static std::unordered_map<std::string, Method> funcs{
#define INSTANCE_HANDLER(name)                                                                                         \
    {                                                                                                                  \
        "tensorflow." #name "Request", [](auto creq, auto &&hcb) {                                                     \
            auto [tfreq, tfresp] = prepareTFCall<tf::name##Request>(creq);                                             \
            auto &resp = *tfresp;                                                                                      \
            hcb.tfresp = std::move(tfresp);                                                                            \
            TFInstance::instance().handle##name(std::move(tfreq), resp, std::forward<decltype(hcb)>(hcb));             \
        }                                                                                                              \
    }

        INSTANCE_HANDLER(CreateSession), INSTANCE_HANDLER(CloseSession),   INSTANCE_HANDLER(ListDevices),
        INSTANCE_HANDLER(Reset),

#undef INSTANCE_HANDLER

#define SESSION_HANDLER(name)                                                                                          \
    {                                                                                                                  \
        "tensorflow." #name "Request", [](auto creq, auto &&hcb) -> void {                                             \
            auto [tfreq, tfresp] = prepareTFCall<tf::name##Request>(creq);                                             \
            auto &resp = *tfresp;                                                                                      \
            hcb.tfresp = std::move(tfresp);                                                                            \
            auto sess = TFInstance::instance().findSession(tfreq->session_handle());                                   \
            sess->handle##name(*tfreq, resp, std::forward<decltype(hcb)>(hcb));                                        \
        }                                                                                                              \
    }

        SESSION_HANDLER(ExtendSession),  SESSION_HANDLER(PartialRunSetup), SESSION_HANDLER(RunStep),
#undef SESSION_HANDLER
    };

    HandlerCallback hcb{std::move(cb), nullptr};
    try {
        auto it = funcs.find(creq.type());
        if (it == funcs.end()) {
            throw TFException(tf::errors::InvalidArgument(creq.type(), " not found in registered custom tasks"));
        }

        VLOG(2) << "Dispatching custom task " << it->first << " of seq " << evenlop.seq();
        it->second(creq, std::move(hcb));
    } catch (const TFException &ex) {
        LOG(ERROR) << "Error when executing custom task " << creq.type() << " of seq " << evenlop.seq() << ": "
                   << ex.what();
        hcb(ex.code());
    }
}

} // namespace salus::oplib::tensorflow
