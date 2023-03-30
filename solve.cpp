#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <iostream>
#include <vector>
#include <set>
#include <cassert>
#include <chrono>
#include <sstream>
#include <numeric>
#include <functional>
#include <thread>
#include <map>

// TODO: move to arguments 
#define N_WORKERS 	2 // Skrzaty
#define N_KILLERS 	1 // Gnomy
#define N_PINS 		2 // Agrafki
#define N_SCOPES 	0 // Celowniki
					  // 
#define GNOME_MESSAGE_TAG 123
		
enum {
	MESSAGE_TYPE_NONE = -1,
	MESSAGE_TYPE_REQUEST,
	MESSAGE_TYPE_ACK,
	MESSAGE_TYPE_CONSUME,
	MESSAGE_TYPE_PRODUCE,
	MESSAGE_TYPE_N,
};

enum { 
	RESOURCE_TYPE_NONE = -1,
	RESOURCE_TYPE_PIN_SCOPE,
	// RESOURCE_TYPE_SCOPE,
	RESOURCE_TYPE_WEAPON,
	RESOURCE_TYPE_N,
};

enum {
	GNOME_STATE_NONE = -1,
	GNOME_STATE_SLEEPING,
	GNOME_STATE_RESTING,
	GNOME_STATE_REQUESTING, 
	GNOME_STATE_INSECTION,
	GNOME_STATE_N,
};

enum {
	GNOME_TYPE_NONE = -1,
	GNOME_TYPE_WORKER,
	GNOME_TYPE_KILLER,
};


// Assumes one resource
class GnomeResourceQueue { 

	struct GnomeQueueEntry {
		int gnome_id, lamport_timestamp;
	};
	
	struct GnomeQueueEntryComparator {
    	bool operator() (GnomeQueueEntry lhs, GnomeQueueEntry rhs) const {
			if (lhs.lamport_timestamp != rhs.lamport_timestamp) return lhs.lamport_timestamp < rhs.lamport_timestamp;
			else return lhs.gnome_id < rhs.gnome_id;
		}
	};
	
	int resource_cnt = 0;
	std::map<int, bool> ack_sent;

	std::set<GnomeQueueEntry, GnomeQueueEntryComparator> req_queue;
public:
	
	std::string get_debug_info() {
		std::stringstream ss;
		ss << "GnomeResourceQueue{" << resource_cnt << " res; q=[";
		for (GnomeQueueEntry entry : req_queue) {
			ss << "(" << entry.gnome_id << "," << entry.lamport_timestamp << "), ";
		}
		ss << "]; acks: ["; 
		for (auto p : ack_sent) {
			ss << "(" << p.first << ", " << p.second << "), ";
		}
		ss << "]}";
		return ss.str();
	}
	
	void init(const std::vector<int>& gnome_ids, int n_resources) {
		resource_cnt = n_resources;
		for (auto id : gnome_ids) {
			ack_sent[id] = false;
		}
	}
	
	
	// REQUEST received -> possible sending ACK 
	// - check if entry is in ack window [0, <resource_cnt>)
	// - * if yes: mark send ack
	void add_request(int gnome_id, int lamport, bool& should_send_ack, int& ack_gnome_id) {
		GnomeQueueEntry entry = { gnome_id, lamport };

		// make sure request does not exist
		assert(req_queue.find(entry) == req_queue.end());

		// returns iterator to inserted element, and true if succeeded
		auto p = req_queue.insert(entry); 
		int idx = std::distance(req_queue.begin(), p.first);

		// new request in ACK window
		if (idx < resource_cnt) {
			should_send_ack = true;
			ack_gnome_id = gnome_id;
		} else {
			should_send_ack = false;
		}
	}

	// CONSUME resource -> remove request, no option in sending ACK
	// - remove entry from Queue
	// - set ack_sent to false for this gnome
	// - decrement resource
	void consume_resource(int gnome_id, int lamport) {
		auto entry_it = req_queue.begin();
		while (entry_it != req_queue.end() && entry_it->gnome_id != gnome_id) entry_it++;

		// make sure gnome was in the queue
		assert(entry_it != req_queue.end());

		// make sure gnome was in the window?
		int idx = std::distance(req_queue.begin(), entry_it);
		assert(idx < resource_cnt);

		// make sure we sent ack to gnome:
		assert(ack_sent.at(gnome_id) == true);
		
		
		// action:
		ack_sent[gnome_id] = false;
		req_queue.erase(entry_it);
		resource_cnt--;
	}
	
	// PRODUCE resource -> possible 1 ack to send (enlarging the ack window)
	// - increment cnt
	// - check if <resource_cnt>-th index needs ack
	void produce_resource(bool& should_send_ack, int &ack_gnome_id) {
		resource_cnt++;
		if (resource_cnt < req_queue.size() + 1) {
			auto entry_it = req_queue.begin();
			std::advance(entry_it, resource_cnt);
			
			ack_gnome_id = entry_it->gnome_id;
			should_send_ack = !(ack_sent.at(entry_it->gnome_id));
		} else {
			should_send_ack = false;
		}
	}
	
	void mark_ack_sent(int gnome_id) {
		assert(ack_sent.at(gnome_id) == false);
		ack_sent[gnome_id] = true;
	}
};

struct GnomeMessage {
	int message_type;
	int resource_type; 
	int lamport_timestamp;
};

class Gnome {
	int tid = -1;
	int type = GNOME_TYPE_NONE; 
	int state = GNOME_STATE_NONE;
	int lamport = 0; 
	int req_resource = RESOURCE_TYPE_NONE;

	std::vector<int> same_type_ids;
	std::vector<int> other_type_ids;

	std::map<int, bool> received_ack;
	int received_ack_cnt = 0;

	std::map<int, GnomeResourceQueue> resource_queues;
	std::array<int, GNOME_STATE_N> state_time; 

	
public:
	Gnome(int g_tid, int g_type, const std::vector<int>& workers, const std::vector<int>& killers) {
		tid = g_tid;
		type = g_type;
		lamport = 0;
		state = GNOME_STATE_SLEEPING;
		req_resource = RESOURCE_TYPE_NONE;
		state_time = { 3, 1, 2, 3 };
		
		if (type == GNOME_TYPE_WORKER) {
			same_type_ids = workers;
			other_type_ids = killers;
		} else if (type == GNOME_TYPE_KILLER) {
			same_type_ids = killers;
			other_type_ids = workers;
		}
		
		clear_received_ack();
	}
	
	void init_resources(const std::vector<std::pair<int, int>>& resources) {
		for (auto p : resources) {
			int r = p.first; 
			int rc = p.second; 
			resource_queues[r] = GnomeResourceQueue();
			resource_queues[r].init(same_type_ids, rc);
		}
	}
	
	// can consume only one resource
	bool all_gnomes_agreed() {
		// all gnomes approved
		return (received_ack_cnt == same_type_ids.size());
	}
	
	void clear_received_ack() {
		for (int id : same_type_ids) {
			received_ack[id] = false;
		}
		received_ack_cnt = 0;
	}

	void act_as_worker() {
		static auto last_transition = std::chrono::system_clock::now();
		
		auto current_time = std::chrono::system_clock::now();
		auto elapsed = std::chrono::duration<float>(current_time - last_transition);
		float seconds = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();

		// transition SLEEPING -> RESTING
		if (state == GNOME_STATE_SLEEPING) {
			// TODO: change
			std::cerr << get_debug_prefix() << "Feeling sleepy... (going sleep for " << state_time[state] << " seconds)" << std::endl;
			std::this_thread::sleep_for(std::chrono::seconds(state_time[state]));
			state = GNOME_STATE_RESTING;
		} else if (seconds < state_time[state]) {
			// not ready yet
			return;
		}
		// transition RESTING -> REQUESTING
		else if (state == GNOME_STATE_RESTING) {
			std::cerr << get_debug_prefix() << "Searching for pin! (waiting for ACKs)" << std::endl;
			req_resource = RESOURCE_TYPE_PIN_SCOPE;

			send_request_resource(req_resource);
			state = GNOME_STATE_REQUESTING;
		}
		
		// transition REQUESTING -> INSECTION
		else if (state == GNOME_STATE_REQUESTING && all_gnomes_agreed()) {
			auto& q = resource_queues[req_resource];
			std::cerr << get_debug_prefix() << "Assembling the next weapon of mass ratstruction! (using pins for next " 
				<< state_time[GNOME_STATE_INSECTION] << " seconds) because all gnomes: " << all_gnomes_agreed() << " - " << q.get_debug_info() << std::endl;

			clear_received_ack();
			send_consume_resource(req_resource);
			state = GNOME_STATE_INSECTION;
				
			req_resource = RESOURCE_TYPE_NONE;
		}
		
		// transition INSECTION -> SLEEPING 
		else if (state == GNOME_STATE_INSECTION) {
			std::cerr << get_debug_prefix() << "Finishing the weapon... (producing killers resources)" << std::endl;
			
			// TODO: make sure to send both after transition
			send_produce_resource(RESOURCE_TYPE_WEAPON);
			state = GNOME_STATE_SLEEPING;
		}

		last_transition = std::chrono::system_clock::now();
	}

	void act_as_killer() {
		static auto last_transition = std::chrono::system_clock::now();
		
		auto current_time = std::chrono::system_clock::now();
		auto elapsed = std::chrono::duration<float>(current_time - last_transition);
		float seconds = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();

		// transition SLEEPING -> RESTING
		if (state == GNOME_STATE_SLEEPING) {
			// TODO: change
			std::cerr << get_debug_prefix() << "Feeling sleepy... (going sleep for " << state_time[state] << " seconds)" << std::endl;
			std::this_thread::sleep_for(std::chrono::seconds(state_time[state]));
			state = GNOME_STATE_RESTING;
		} else if (seconds < state_time[state]) {
			// not ready yet
			return;
		}
		// transition RESTING -> REQUESTING
		else if (state == GNOME_STATE_RESTING) {
			std::cerr << get_debug_prefix() << "I need weapon! (waiting for ACKs)" << std::endl;
			req_resource = RESOURCE_TYPE_WEAPON;

			send_request_resource(req_resource);
			state = GNOME_STATE_REQUESTING;
		}
		
		// transition REQUESTING -> INSECTION
		else if (state == GNOME_STATE_REQUESTING && all_gnomes_agreed()) {
			std::cerr << get_debug_prefix() << "Sending the next RAT to the moon, boyz! (using weapon for next " 
				<< state_time[GNOME_STATE_INSECTION] << " seconds)" << std::endl;

			clear_received_ack();
			send_consume_resource(req_resource);
			state = GNOME_STATE_INSECTION;
				
			req_resource = RESOURCE_TYPE_NONE;
		}
		
		// transition INSECTION -> SLEEPING 
		else if (state == GNOME_STATE_INSECTION) {
			std::cerr << get_debug_prefix() << "Dissasembling weapon... (producing workers resources)" << std::endl;
			
			// TODO: make sure to send both after transition
			send_produce_resource(RESOURCE_TYPE_PIN_SCOPE);
			state = GNOME_STATE_SLEEPING;
		}

		last_transition = std::chrono::system_clock::now();
	}

	void perform() {
		if (type == GNOME_TYPE_NONE) {
			std::cout << get_debug_prefix() << "I am type NONE, returning..." << std::endl;
			return;
		}
		
		while (true) {
			if (type == GNOME_TYPE_WORKER) act_as_worker();
			else if (type == GNOME_TYPE_KILLER) act_as_killer();

			MPI_Status status;
			int probe_flag;
			int probe_ret = MPI_Iprobe(MPI_ANY_SOURCE, GNOME_MESSAGE_TAG, MPI_COMM_WORLD, &probe_flag, &status);
			
			if (!probe_flag) continue;

			int buffer[3]; // type, timestamp, resource
			MPI_Recv(buffer, 3, MPI_INT, MPI_ANY_SOURCE, GNOME_MESSAGE_TAG, MPI_COMM_WORLD, &status);
			GnomeMessage gm = { buffer[0], buffer[1], buffer[2] };
			react_to_message(gm, status.MPI_SOURCE);
		}
	}
private:

	std::string get_debug_prefix() {
		std::stringstream iss;
		char type_letter = '?';
		if (type == GNOME_TYPE_WORKER) type_letter = 'W';
		else if (type == GNOME_TYPE_KILLER) type_letter = 'K';
		iss << type_letter << "[" << tid << "] [t" << lamport << "]: ";
		return iss.str();
	}
	
	void react_to_message(GnomeMessage gm, int src_gnome_id) {
		lamport = std::max(gm.lamport_timestamp, lamport) + 1;
		
		// if (src_gnome_id == tid) std::cerr << get_debug_prefix() << "received from myself! " << gm.message_type << std::endl;
		
		auto& queue = resource_queues[gm.resource_type];
		bool should_send_ack = false;
		int gnome_id = -1;
		
		switch (gm.message_type) {
			case MESSAGE_TYPE_PRODUCE: {
				queue.produce_resource(should_send_ack, gnome_id);
				std::cerr << get_debug_prefix() << "...received PRODUCE from " << src_gnome_id << "(something new: " << should_send_ack << ")" << std::endl;
			} break;
			case MESSAGE_TYPE_CONSUME: {
				std::cerr << get_debug_prefix() << "...received CONSUME from " << src_gnome_id << ", -> " << queue.get_debug_info() << std::endl;
				queue.consume_resource(src_gnome_id, gm.lamport_timestamp);
			} break;
			case MESSAGE_TYPE_REQUEST: {
				queue.add_request(src_gnome_id, gm.lamport_timestamp, should_send_ack, gnome_id);
				std::cerr << get_debug_prefix() << "...received REQUEST from " << src_gnome_id << "(should send? " << should_send_ack << ", " << gnome_id << ")" << std::endl;
			} break;
			case MESSAGE_TYPE_ACK: {
				assert(received_ack[src_gnome_id] == false);
				received_ack[src_gnome_id] = true;
				received_ack_cnt++;
			} break;
		}

		if (should_send_ack) {
			send_ack_resource(gnome_id, gm.resource_type);
			queue.mark_ack_sent(gnome_id);
		}
	}
	
	
	void send_ack_resource(int gnome_id, int resource) {
		lamport++;
		int buffer[3] = { MESSAGE_TYPE_ACK, resource, lamport };
		MPI_Send(buffer, 3, MPI_INT, gnome_id, GNOME_MESSAGE_TAG, MPI_COMM_WORLD);
	}
	
	void send_request_resource(int resource) {
		// If I am worker/killer -> broadcast message to all workers/killers that I want to take resource
		lamport++;
		int buffer[3] = { MESSAGE_TYPE_REQUEST, resource, lamport };
		for (auto gnome_id : same_type_ids) {
			MPI_Send(buffer, 3, MPI_INT, gnome_id, GNOME_MESSAGE_TAG, MPI_COMM_WORLD);
		}
	}

	void send_consume_resource(int resource) {
		// If I am worker/killer -> broadcast message to all killers/workers that I want released resource
		lamport++;
		int buffer[3] = { MESSAGE_TYPE_CONSUME, resource, lamport };
		for (auto gnome_id : same_type_ids) {
			MPI_Send(buffer, 3, MPI_INT, gnome_id, GNOME_MESSAGE_TAG, MPI_COMM_WORLD);
		}
	}
	
	void send_produce_resource(int resource) {
		lamport++;
		int buffer[3] = { MESSAGE_TYPE_PRODUCE, resource, lamport };
		for (auto gnome_id : other_type_ids) {
			MPI_Send(buffer, 3, MPI_INT, gnome_id, GNOME_MESSAGE_TAG, MPI_COMM_WORLD);
		}
	}

};


int assign_gnome_roles(std::vector<int>& workers, std::vector<int>& killers, int n_workers, int n_killers, int my_tid) {
	// Return my Gnome Role 
	// tid in [0 : n_workers) -> gnome worker
	// tid in [n_workers : n_workers + n_killers) -> gnome killer
	// tid in [n_workers + n_killers : ...)  -> none
	// + fill vectors with ids of other gnomes
	
	for (int i = 0; i < n_workers; i++) {
		// if (i != my_tid) 
		workers.push_back(i);
	}
	for (int i = n_workers; i < n_workers + n_killers; i++) {
		// if (i != my_tid) 
		killers.push_back(i);
	}

	if (my_tid < n_workers) {
		return GNOME_TYPE_WORKER;
	} else if (my_tid < n_workers + n_killers) {
		return GNOME_TYPE_KILLER;
	} else {
		return GNOME_TYPE_NONE;
	}
}

int main(int argc, char **argv) {
	MPI_Status status;
	int tid, size;
	int provided;

	MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided); //Musi być w każdym programie na początku
	MPI_Comm_size( MPI_COMM_WORLD, &size );
	MPI_Comm_rank( MPI_COMM_WORLD, &tid );

	printf("My id is %d from %d\n",tid, size);
	
	// We can pass maximums for both workers and killers
	// - if possible we try to get all workers
	// - we try to assign what left to killers
	int n_workers = std::min(N_WORKERS, size);
	int n_killers = std::min(size - n_workers, N_KILLERS);
	
	std::vector<int> workers;
	std::vector<int> killers;
	int my_type = assign_gnome_roles(workers, killers, n_workers, n_killers, tid);
	
	// TODO: change resources to both Pins and Scopes
	
	Gnome gnome(tid, my_type, workers, killers) ;
	std::vector<std::pair<int, int>> resources;

	if (my_type == GNOME_TYPE_KILLER) {
		resources.push_back({RESOURCE_TYPE_WEAPON, 1});
	} else if (my_type == GNOME_TYPE_WORKER) {
		resources.push_back({RESOURCE_TYPE_PIN_SCOPE, N_PINS});
		// TODO: Add second resource
	}
	
	gnome.init_resources(resources);
	gnome.perform();

	MPI_Finalize(); // Musi być w każdym programie na końcu
}
