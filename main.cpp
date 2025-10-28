#include <iostream>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/bind_executor.hpp>
#include <boost/asio/post.hpp>
#include <thread>
#include <memory>
#include <string>
#include <vector>
#include <mutex>
#include <nlohmann/json.hpp>
#include <chrono>
#include <iomanip>
#include "GameObject.h"
#include "GameWorld.h"

using namespace std;

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = net::ip::tcp;
using json = nlohmann::json;

//전방 선언
class GameServer;

//웹 소켓 세션 클래스
class Session : public enable_shared_from_this<Session>
{
private:
	websocket::stream<tcp::socket> ws_;
	beast::flat_buffer buffer_;
	int playerId_;
	string nickname_;
	GameServer *server_;
	bool isAlive_;
	bool hasJoined_; // JOIN_REQUEST를 받았는지 확인

	net::strand<net::io_context::executor_type> strand_;
	vector<string> writeQueue_;
	mutex queueMutex_;
	bool isWriting_ = false;
	string curentWriteMessage_;

public:
	Session(tcp::socket socket, GameServer *server, net::io_context &ioc)
		: ws_(move(socket)),
		playerId_(-1),
		server_(server),
		isAlive_(true),
		hasJoined_(false),
		strand_(net::make_strand(ioc.get_executor()))
	{
	}

	int getPlayerId() const { return playerId_; }
	string getNickname() const { return nickname_; }
	bool isAlive() const { return isAlive_; }
	bool hasJoined() const { return hasJoined_; }

	void run()
	{
		//websocket 핸드셰이크
		//모든 비동기 작업을 strand_를 통해 실행하도록 bind_executor 사용추가
		ws_.async_accept(
			net::bind_executor(strand_,
				[self = shared_from_this()](beast::error_code ec)
				{
					if (!ec)
					{
						cout << "Client connected, waiting for JOIN_REQUEST..." << endl;
						self->doRead();
					}
				})
		);
	}

	void sendJoinResponse(bool success, int playerId, string nickname, string message = "")
	{
		json response;
		response["type"] = 2; // JOIN_RESPONSE
		response["success"] = success;

		if (success)
		{
			response["playerId"] = playerId;
			response["nickname"] = nickname;
		}
		else
		{
			response["message"] = message;
		}

		send(response.dump());
	}

	void doRead()
	{
		// 읽기 작업도 strand_를 통해 실행추가
		ws_.async_read(
			buffer_,
			net::bind_executor(strand_,
				[self = shared_from_this()](beast::error_code ec, size_t bytes)
				{
					if (!ec)
					{
						//받은 메시지 처리
						string message = beast::buffers_to_string(self->buffer_.data());
						self->buffer_.consume(self->buffer_.size());

						self->handleMessage(message);
						self->doRead();
					}
					else
					{
						if (self->hasJoined_)
						{
							cout << "Player " << self->playerId_ << " (" << self->nickname_ << ") disconnected" << endl;
						}
						else
						{
							cout << "Client disconnected before joining" << endl;
						}
						self->isAlive_ = false;
					}
				})
		);
	}

	// 비동기 큐 구현
	void send(const string &message)
	{
		bool startWrite = false;
		// 큐는 락걸고 작업해야하니 스코프안에서
		{
			lock_guard<mutex> lock(queueMutex_);
			writeQueue_.push_back(message);

			if (!isWriting_)
			{
				isWriting_ = true;
				startWrite = true;
			}
		}

		if (startWrite)
		{
			net::post(strand_, [self = shared_from_this()]() {
				self->doWrite();
				});
		}
	}

private:
	// 비동기 쓰기 루프
	void doWrite()
	{
		// 큐는 락걸고 작업해야하니 스코프안에서
		{
			lock_guard<mutex> lock(queueMutex_);

			// 큐가 비면 쓰기 종료후 리턴
			if (writeQueue_.empty())
			{
				isWriting_ = false;
				return;
			}

			curentWriteMessage_ = std::move(writeQueue_.front());
			writeQueue_.erase(writeQueue_.begin());
		}

		ws_.text(true);
		ws_.async_write(
			net::buffer(curentWriteMessage_),
			net::bind_executor(strand_,
				[self = shared_from_this()](beast::error_code ec, size_t bytes)
				{
					if (!ec)
					{
						// 다음 메시지 전송
						self->doWrite();
					}
					else
					{
						cerr << "Send error: " << ec.message() << endl;
						self->isAlive_ = false;
					}
				})
		);
	}

public:
	void handleMessage(const string &message); // 전방 선언
	void sendGameState(); // 전방 선언
};

class GameServer
{
private:
	net::io_context ioc_;
	tcp::acceptor acceptor_;
	vector<shared_ptr<Session>> sessions_;
	mutex sessionsMutex_;

	GameWorld gameWorld_;
	mutex worldMutex_;

	//게임 루프용
	const int TARGET_FPS = 60;
	const float FIXED_DELTA_TIME = 1.0f / 60.0f;

	const int MAX_PLAYERS = 50;

	// TPS 측정용 추가
	int tickCount_ = 0;
	chrono::steady_clock::time_point lastTPSUpdate_;
	float currentTPS_ = 0.0f;

	thread gameLoopThread_;
	vector<thread> iocThreads_;
	atomic<bool> running_;

	int broadcaseCounter_ = 0;

	json getGameStateInternal()
	{
		json data;
		data["type"] = 4; // GAME_STATE

		json playersArray = json::array();
		for (const auto &player : gameWorld_.getPlayers())
		{
			if (player != nullptr)
			{
				json p;
				p["id"] = player->id;
				p["nickname"] = player->nickname;
				p["pos"] = { player->position.x, player->position.y, player->position.z };
				p["vel"] = { player->velocity.x, player->velocity.y, player->velocity.z };
				p["color"] = { player->color.r, player->color.g, player->color.b };
				playersArray.push_back(p);
			}
		}
		data["players"] = playersArray;

		// 더미 배열 추가
		json dummiesArray = json::array();
		for (const auto &dummy : gameWorld_.getDummies())
		{
			json d;
			d["id"] = dummy->id;
			d["pos"] = { dummy->position.x, dummy->position.y, dummy->position.z };
			dummiesArray.push_back(d);
		}
		data["dummies"] = dummiesArray;

		return data;
	}

public:
	GameServer(int port)
		: acceptor_(ioc_, tcp::endpoint(tcp::v4(), port))
		, running_(false)
	{
		lastTPSUpdate_ = chrono::steady_clock::now();
	}
	~GameServer()
	{
		stop();
	}

	void start()
	{
		cout << "Game Server Started on port 9002" << endl;
		cout << "Target FPS: " << TARGET_FPS << endl;
		cout << "Fixed Delta Time: " << FIXED_DELTA_TIME << "s" << endl;
		cout << "Waiting for players (max " << MAX_PLAYERS << ")..." << endl;
		doAccept();

		running_ = true;
		gameLoopThread_ = thread([this]() {this->gameLoopThreadFunc(); });
	}

	void run()
	{
		// CPU 코어 수 만큼 I/O 스레드 생성 예정
		auto const thread_count = std::max<int>(1, std::thread::hardware_concurrency());
		cout << "Starting " << thread_count << " I/O threads" << endl;

		iocThreads_.reserve(thread_count - 1);

		// I/O 스레드 실제 생성 (N-1개)
		for (int i = 0; i < thread_count - 1; ++i)
		{
			iocThreads_.emplace_back([this] {
				ioc_.run();
				});
		}

		// N번째는 메인 스레드
		ioc_.run();

		// 서버 종료시 모든 스레드 수거
		for (auto &t : iocThreads_)
		{
			if (t.joinable())
				t.join();
		}
	}

	void stop()
	{
		running_ = false;
		if (gameLoopThread_.joinable())
		{
			gameLoopThread_.join();
		}

		if (!ioc_.stopped())
		{
			ioc_.stop();
		}
	}

	int joinPlayer(string nickname, Color color)
	{
		lock_guard<mutex> lock(worldMutex_);
		return gameWorld_.addPlayer(nickname, color);
	}

	void setPlayerInput(int playerId, const Vector3 &movement)
	{
		lock_guard<mutex> lock(worldMutex_);
		gameWorld_.setPlayerInput(playerId, movement);
	}

	void playerJump(int playerId)
	{
		lock_guard<mutex> lock(worldMutex_);
		gameWorld_.playerJump(playerId);
	}

	void removePlayer(int playerId)
	{
		lock_guard<mutex> lock(worldMutex_);
		gameWorld_.removePlayer(playerId);
	}

	void spawnDummies(int count)
	{
		string updateData;
		{
			lock_guard<mutex> lock(worldMutex_);
			gameWorld_.spawnDummies(count);
			updateData = getGameStateInternal().dump();
		}
		broadcast(updateData);
	}

	void deleteAllDummies()
	{
		string updateData;
		{
			lock_guard<mutex> lock(worldMutex_);
			gameWorld_.deleteAllDummies();
			updateData = getGameStateInternal().dump();
		}
		broadcast(updateData);
	}

	json getGameState()
	{
		lock_guard<mutex> lock(worldMutex_);
		return getGameStateInternal();
	}

	void broadcast(const string &message)
	{
		lock_guard<mutex> lock(sessionsMutex_);
		for (auto &session : sessions_)
		{
			if (session->isAlive() && session->hasJoined())
			{
				session->send(message);
			}
		}
	}

	int getConnectedPlayerCount()
	{
		lock_guard<mutex> lock(sessionsMutex_);
		int count = 0;
		for (auto &session : sessions_)
		{
			if (session->hasJoined())
			{
				count++;
			}
		}
		return count;
	}

private:
	void doAccept()
	{
		acceptor_.async_accept(
			[this](beast::error_code ec, tcp::socket socket)
			{
				if (!ec)
				{
					// 세션만 생성, playerId는 JOIN_REQUEST에서 할당
					auto session = make_shared<Session>(move(socket), this, ioc_);
					{
						lock_guard<mutex> lock(sessionsMutex_);
						sessions_.push_back(session);
					}

					session->run();
				}
				doAccept();
			});
	}

	void gameLoopThreadFunc()
	{
		using namespace chrono;

		const auto frameDuration = microseconds(100000 / TARGET_FPS);
		auto nextFrameTime = steady_clock::now();

		while (running_)
		{
			auto frameStart = steady_clock::now();

			gameUpdate();

			//다음 프레임 시간 계산
			nextFrameTime += frameDuration;

			//너무 늦었으면 리셋
			auto now = steady_clock::now();
			if (nextFrameTime < now)
			{
				nextFrameTime = now + frameDuration;
			}
			this_thread::sleep_until(nextFrameTime);
		}
	}

	void gameUpdate()
	{
		string updateData;
		//게임 월드 업데이트
		{
			lock_guard<mutex> lock(worldMutex_);
			gameWorld_.update(FIXED_DELTA_TIME);
			updateData = getGameStateInternal().dump();
		}

		// 브로드캐스트
		broadcast(updateData);

		//죽은 세션 제거
		{
			lock_guard<mutex> lock(sessionsMutex_);

			for (auto it = sessions_.begin(); it != sessions_.end();)
			{
				if (!(*it)->isAlive())
				{
					if ((*it)->hasJoined())
					{
						int playerId = (*it)->getPlayerId();
						removePlayer(playerId);
					}
					it = sessions_.erase(it);
				}
				else
				{
					++it;
				}
			}
		}
		updateTPS();
	}

	void updateTPS()
	{
		tickCount_++;

		auto now = chrono::steady_clock::now();
		auto elapsed = chrono::duration_cast<chrono::milliseconds>(now - lastTPSUpdate_).count();

		// 1초마다 TPS 출력
		if (elapsed >= 1000)
		{
			currentTPS_ = tickCount_ * 1000.0f / elapsed;

			// 콘솔 클리어 (Windows)
#ifdef _WIN32
			system("cls");
#else
			system("clear");
#endif

			// 서버 정보 출력
			cout << "=== Game Server Status ===" << endl;
			cout << "TPS: " << fixed << setprecision(1) << currentTPS_ << endl;
			cout << "Tick Time: " << fixed << setprecision(2)
				<< (1000.0f / currentTPS_) << " ms" << endl;

			// 색상 표시 (TPS에 따라)
			if (currentTPS_ >= TARGET_FPS * 0.95f)
				cout << "Performance: EXCELLENT" << endl;
			else if (currentTPS_ >= TARGET_FPS * 0.80f)
				cout << "Performance: GOOD" << endl;
			else
				cout << "Performance: POOR" << endl;

			// 플레이어 정보
			cout << "Connected Players: " << getConnectedPlayerCount() << " / " << MAX_PLAYERS << endl;

			// 리셋
			tickCount_ = 0;
			lastTPSUpdate_ = now;
		}
	}

};

void Session::sendGameState()
{
	json gameState = server_->getGameState();
	send(gameState.dump());
}

void Session::handleMessage(const string &message)
{
	try
	{
		json data = json::parse(message);
		int msgType = data["type"];

		switch (msgType)
		{
		case 1: // JOIN_REQUEST
		{
			if (hasJoined_)
			{
				cout << "Player already joined, ignoring duplicate JOIN_REQUEST" << endl;
				return;
			}

			string requestedNickname = data["nickname"];

			// 색상 정보 파싱 (선택사항)
			Color playerColor(1.0f, 1.0f, 1.0f); // 기본값 흰색
			if (data.contains("color") && data["color"].is_array() && data["color"].size() >= 3)
			{
				playerColor.r = data["color"][0];
				playerColor.g = data["color"][1];
				playerColor.b = data["color"][2];
			}

			// 플레이어 추가 시도
			int assignedId = server_->joinPlayer(requestedNickname, playerColor);

			if (assignedId != -1)
			{
				// 성공
				playerId_ = assignedId;
				nickname_ = requestedNickname;
				hasJoined_ = true;

				cout << "Player " << playerId_ << " (" << nickname_ << ") joined with color ("
					<< playerColor.r << ", " << playerColor.g << ", " << playerColor.b << ")" << endl;

				sendJoinResponse(true, playerId_, nickname_);

				// 초기 게임 상태 전송
				this_thread::sleep_for(chrono::milliseconds(100)); // ?? 왜 잠드는 것인지 ??
				sendGameState();
			}
			else
			{
				// 실패 (서버 만원)
				cout << "Server full, rejecting join request" << endl;
				sendJoinResponse(false, -1, "", "Server is full (50/50 players)");
			}
			break;
		}

		case 3: // PLAYER_INPUT
		{
			if (!hasJoined_)
			{
				cerr << "Received INPUT from non-joined client" << endl;
				return;
			}

			int pid = data["playerId"];
			if (pid != playerId_)
			{
				cerr << "PlayerId mismatch!" << endl;
				return;
			}

			Vector3 movement(
				data["x"],
				data["y"],
				data["z"]
			);

			server_->setPlayerInput(playerId_, movement);
			break;
		}

		case 5: // JUMP_COMMAND
		{
			if (!hasJoined_)
			{
				cerr << "Received JUMP from non-joined client" << endl;
				return;
			}

			int pid = data["playerId"];
			if (pid != playerId_)
			{
				cerr << "PlayerId mismatch!" << endl;
				return;
			}

			server_->playerJump(playerId_);
			break;
		}

		case 6: // SPAWN_DUMMIES
		{
			if (!hasJoined_)
			{
				cerr << "Received SPAWN_DUMMIES from non-joined client" << endl;
				return;
			}

			int count = data.value("count", 10);
			server_->spawnDummies(count);
			cout << "Spawning " << count << " dummies requested by Player " << playerId_ << endl;
			break;
		}

		case 7: // DELETE_ALL_DUMMIES
		{
			if (!hasJoined_)
			{
				cerr << "Received DELETE_ALL_DUMMIES from non-joined client" << endl;
				return;
			}

			server_->deleteAllDummies();
			cout << "Delete all dummies requested by Player " << playerId_ << endl;
			break;
		}

		default:
			cerr << "Unknown message type: " << msgType << endl;
			break;
		}
	}
	catch (const exception &e)
	{
		cerr << "Message parse error: " << e.what() << endl;
	}
}

int main()
{
	try
	{
		GameServer server(9002);
		server.start();
		server.run();
	}
	catch (const exception &e)
	{
		cerr << "Fatal Error: " << e.what() << endl;
		return 1;
	}
	return 0;
}
