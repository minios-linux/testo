
#include "NNClient.hpp"
#include "Logger.hpp"
#include "Exceptions.hpp"
#include "Utils.hpp"

#include <coro/Timer.h>
#include <iostream>

using namespace std::chrono_literals;

static const VersionNumber client_version(TESTO_VERSION);
static const VersionNumber minimal_server_version(3, 2, 0);

asio::ip::tcp::endpoint parse_tcp_endpoint(const std::string& endpoint);

NNClient::NNClient(const std::string& endpoint_, const std::string& allowed_sharing_directory_):
	endpoint(parse_tcp_endpoint(endpoint_)),
	channel(new Channel(Socket())),
	allowed_sharing_directory(fs::canonical(allowed_sharing_directory_))
{
	TRACE();

	establish_connection();
}

NNClient::~NNClient() {
	TRACE();
}

bool is_connection_lost(const std::error_code& code) {
	int value = code.value();
	if (value == ECONNABORTED ||
		value == ECONNRESET ||
		value == ENETDOWN ||
		value == ENETRESET ||
		value == ENOENT ||
		value == EPIPE ||
		value == ENOTCONN)
	{
		return true;
	} else {
		return false;
	}
}

void NNClient::establish_connection() {
	establish_connection_wrapper([&] {
		channel->socket = Socket();
		channel->socket.connect(endpoint);
	});

	channel->send(create_handshake_request(client_version));
	nlohmann::json response = channel->recv();
	std::string type = response.at("type");
	if (type == ERROR_RESPONSE) {
		server_version = VersionNumber(3, 0, 0);
	} else if (type == HANDSHAKE_RESPONSE) {
		server_version = response.at("server_version").get<std::string>();
	} else {
		throw std::runtime_error(std::string("Unexpected message type: ") + type);
	}
	if (server_version < minimal_server_version) {
		throw std::runtime_error("Testo NN Server has an incompatible version. You should update it to the version " + minimal_server_version.to_string() + " or higher");
	}
}

nlohmann::json NNClient::receive_response() {
	nlohmann::json response = channel->recv();
	std::string type = response.at("type");
	if (type == ERROR_RESPONSE) {
		std::string message = response.at("data");
		std::string failure_category = response.at("failure_category");
		throw ExceptionWithCategory(message, failure_category);
	} else if (type == CONTINUE_ERROR_RESPONSE) {
		std::string message = response.at("data");
		throw ContinueError(message);
	}
	return response;
}

void NNClient::establish_connection_wrapper(const std::function<void()>& fn) {
	for (size_t i = 0; i < establish_connection_tries; ++i) {
		try {
			return fn();
		} catch (const std::exception& error) {
			std::cerr << error.what() << std::endl;
			if (i < (establish_connection_tries - 1)) {
				std::cerr << "Failed to connect to the server, reconnecting ...\n";
				coro::Timer timer;
				timer.waitFor(2s);
			}
		}
	}

	throw std::runtime_error("Exceeding the number of attempts to connect to the server");
}

nlohmann::json NNClient::rcp_wrapper(const std::function<nlohmann::json()>& fn) {
	for (size_t i = 0; i < rpc_tries; ++i) {
		try {
			return fn();
		} catch (const std::system_error& error) {
			if (is_connection_lost(error.code())) {
				std::cerr << error.what() << std::endl;
				if (i < (rpc_tries - 1)) {
					std::cerr << "Lost the connection to the server, reconnecting...\n";
					establish_connection();
				}
			} else {
				throw;
			}
		}
	}
	throw std::runtime_error("Exceeding the number of attempts to execute RPC");
}

static bool is_path_inside(const fs::path& path, const fs::path& directory) {
	auto path_it = path.begin();
	for (auto dir_it = directory.begin(); dir_it != directory.end(); ++dir_it, ++path_it) {
		if (path_it == path.end() || *path_it != *dir_it) {
			return false;
		}
	}
	return true;
}

nlohmann::json NNClient::eval_js(const stb::Image<stb::RGB>* image, const std::string& script) {
	return rcp_wrapper([&] {
		channel->send(create_js_eval_request(*image, script));

		while (true) {
			nlohmann::json response = receive_response();
			std::string type = response.at("type");
			if (type == REF_IMAGE_REQUEST) {
				std::string ref_file_path = response.at("data");
				fs::path canonical_ref_path;
				try {
					canonical_ref_path = fs::canonical(ref_file_path);
				} catch (const std::exception&) {
					throw std::runtime_error("NN server requested image " + ref_file_path + " but the path cannot be resolved");
				}
				if (!is_path_inside(canonical_ref_path, allowed_sharing_directory)) {
					throw std::runtime_error("NN server requested image outside allowed sharing directory: " + canonical_ref_path.generic_string());
				}

				stb::Image<stb::RGBA> ref_image;
				try {
					ref_image = stb::Image<stb::RGBA>(canonical_ref_path.generic_string());
				} catch (const std::exception& error) {
					std::throw_with_nested(std::runtime_error("NN server requested image " + ref_file_path + " but we failed to open the file"));
				}

				channel->send(create_ref_image_response(ref_image));
				continue;
			} else if (type == JS_EVAL_RESPONSE) {
				return response;
			} else {
				throw std::runtime_error(std::string("Unexpected message type: ") + type);
			}
		}
	});
}
