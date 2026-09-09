#include <catch.hpp>

#include "../../testo_guest_additions/src/Channel.hpp"

#include <algorithm>
#include <cstring>
#include <vector>

namespace {

struct FragmentingChannel: Channel {
	std::vector<uint8_t> incoming;
	size_t offset = 0;
	std::vector<uint8_t> outgoing;

	size_t read(uint8_t* data, size_t size) override {
		if (offset >= incoming.size()) return 0;
		const size_t n = std::min<size_t>(1, size);
		std::memcpy(data, incoming.data() + offset, n);
		offset += n;
		return n;
	}

	size_t write(uint8_t* data, size_t size) override {
		outgoing.insert(outgoing.end(), data, data + size);
		return size;
	}
};

void queue_frame(FragmentingChannel& channel, const nlohmann::json& message) {
	const std::string text = message.dump();
	const uint32_t size = static_cast<uint32_t>(text.size());
	const auto* size_bytes = reinterpret_cast<const uint8_t*>(&size);
	channel.incoming.insert(channel.incoming.end(), size_bytes, size_bytes + sizeof(size));
	channel.incoming.insert(channel.incoming.end(), text.begin(), text.end());
}

} // namespace

TEST_CASE("guest additions channel accepts fragmented frame headers") {
	FragmentingChannel channel;
	queue_frame(channel, {
		{"method", "check_avaliable"},
		{"version", "9.9.9"},
	});

	const auto message = channel.receive();
	REQUIRE(message.at("method") == "check_avaliable");
	REQUIRE(message.at("version") == "9.9.9");
	REQUIRE(channel.offset == channel.incoming.size());
}
