#include <catch.hpp>
#include "../../testo_guest_additions_protocol/GuestAdditions.hpp"

#include <cstring>
#include <stdexcept>
#include <vector>

namespace {
struct FakeGuestAdditions: GuestAdditions {
	std::vector<uint8_t> sent;
	std::vector<uint8_t> incoming;
	size_t incoming_offset = 0;

	void queue_response(nlohmann::json response) {
		response["version"] = "9.9.9";
		auto text = response.dump();
		uint32_t size = static_cast<uint32_t>(text.size());
		auto* size_bytes = reinterpret_cast<uint8_t*>(&size);
		incoming.insert(incoming.end(), size_bytes, size_bytes + sizeof(size));
		incoming.insert(incoming.end(), text.begin(), text.end());
	}

	nlohmann::json sent_request() const {
		if (sent.size() < sizeof(uint32_t)) throw std::runtime_error("missing request");
		uint32_t size = 0;
		std::memcpy(&size, sent.data(), sizeof(size));
		if (sent.size() != sizeof(size) + size) throw std::runtime_error("invalid request size");
		return nlohmann::json::parse(sent.begin() + sizeof(size), sent.end());
	}

protected:
	void send_raw(const uint8_t* data, size_t size) override {
		sent.insert(sent.end(), data, data + size);
	}

	void recv_raw(uint8_t* data, size_t size) override {
		if (incoming_offset + size > incoming.size()) {
			throw std::runtime_error("not enough queued response data");
		}
		std::memcpy(data, incoming.data() + incoming_offset, size);
		incoming_offset += size;
	}
};

}

TEST_CASE("current GA get_file_info request shape") {
	FakeGuestAdditions ga;
	ga.queue_response({
		{"success", true},
		{"result", {{"size", 123}}}
	});

	auto info = ga.get_file_info("/tmp/payload.bin");
	REQUIRE(info.at("size").get<uint64_t>() == 123);

	auto request = ga.sent_request();
	REQUIRE(request.at("method") == "get_file_info");
	REQUIRE(request.at("args").is_object());
	REQUIRE(request.at("args").at("path") == "/tmp/payload.bin");
	REQUIRE(request.at("version").is_string());
}

TEST_CASE("generated exec scripts remain in the guest") {
	FakeGuestAdditions ga;

	ga.remove_from_guest("/tmp/cleanup.sh");
	REQUIRE(ga.sent.empty());
}
