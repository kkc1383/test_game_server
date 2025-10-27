#pragma once
#include "GameObject.h"
#include "Physicsworld.h"
#include <vector>
#include <memory>
#include <random>
#include <array>

using namespace std;

class GameWorld
{
private:
	array<unique_ptr<Player>, 50> players_;
	vector<unique_ptr<DummyObject>> dummies_;

	unique_ptr<PhysicsWorld> physicsWorld_;

	const float PLAYER_SPEED = 5.0f;
	const float MAP_SIZE = 25.0f;

	mt19937 rng_; // �����Լ�����
	int nextDummyId_ = 0; //���� Id ī����

public:
	GameWorld()
	{
		rng_.seed(random_device{}());
		physicsWorld_ = make_unique<PhysicsWorld>();
	}

	int addPlayer(string nickname = "Player", Color color = Color(1.0f, 1.0f, 1.0f))
	{
		for (int i = 0; i < 50; i++)
		{
			if (players_[i] == nullptr)
			{
				//시작 위치 (원형으로 배치)
				float angle = i * 3.14159f * 2.0f / 50.0f;
				Vector3 startPos(
					cos(angle) * 8.0f,
					1.0f,
					sin(angle) * 8.0f
				);

				players_[i] = make_unique<Player>(i, startPos, color, nickname);

				//PhysX Actor 생성
				physicsWorld_->createPlayerActor(i, startPos);

				cout << "Player " << i << " (" << nickname << ") joined (slot assigned)" << endl;
				return i;
			}
		}
		cout << "No available slots!" << endl;
		return -1;
	}
	void removePlayer(int playerId)
	{
		if (playerId >= 0 && playerId < 50 && players_[playerId] != nullptr)
		{
			cout << "Player " << playerId << " removed (slot freed)" << endl;
			physicsWorld_->removePlayer(playerId);
			players_[playerId].reset();
		}
	}
	void spawnDummies(int count = 10)
	{
		uniform_real_distribution<float> posDist(-MAP_SIZE * 0.35f, MAP_SIZE * 0.35f);
		
		for (int i = 0; i < count; ++i)
		{
			Vector3 pos(
				posDist(rng_),
				2.0f,
				posDist(rng_)
			);
			int dummyId = nextDummyId_++;

			dummies_.push_back(make_unique<DummyObject>(dummyId, pos, Vector3()));
			physicsWorld_->createDummyActor(dummyId, pos);
		}
	}
	void deleteAllDummies()
	{
		//physX Actor ����
		for (auto& dummy : dummies_)
		{
			physicsWorld_->removeDummy(dummy->id);
		}
		//���� ����
		dummies_.clear();
	}
	// �÷��̾� �Է� ����
	void setPlayerInput(int playerId, const Vector3& movement)
	{
		if (playerId>=0 && playerId < 50 && players_[playerId]!=nullptr)
		{
			players_[playerId]->inputMovement = movement;
		}
	}

	// 플레이어 점프
	void playerJump(int playerId)
	{
		if (playerId >= 0 && playerId < 50 && players_[playerId] != nullptr)
		{
			physicsWorld_->applyPlayerJump(playerId);
		}
	}

	//���� ������Ʈ( ���� �ùķ��̼�)
	void update(float deltaTime)
	{
		// �÷��̾� �Է��� PhysX�� ����
		for (int i = 0; i < 50; ++i)
		{
			if (players_[i] != nullptr && players_[i]->active)
			{
				physicsWorld_->applyPlayerInput(i, players_[i]->inputMovement);
			}
		}

		//PhysX �ùķ��̼�
		physicsWorld_->simulate(deltaTime);

		// PhysX ����� ���� ������Ʈ�� ����ȭ
		for (int i = 0; i < 50; ++i)
		{
			if (players_[i] != nullptr)
			{
				players_[i]->position = physicsWorld_->getPlayerPosition(i);
				players_[i]->velocity = physicsWorld_->getPlayerVelocity(i);
			}
		}

		for (auto& dummy : dummies_)
		{
			dummy->position = physicsWorld_->getDummyPosition(dummy->id);
		}
	}
	//Getter
	const array<unique_ptr<Player>,50>& getPlayers() const { return players_; }
	const vector<unique_ptr<DummyObject>>& getDummies() const { return dummies_; }
};
