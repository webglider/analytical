/******************************************************************************
This source code is licensed under the MIT license found in the
LICENSE file in the root directory of this source tree.
*******************************************************************************/

#include "AnalyticalNetwork.hh"

using namespace Analytical;

std::shared_ptr<Analytical::EventQueue> AnalyticalNetwork::event_queue;

std::shared_ptr<Analytical::Topology> AnalyticalNetwork::topology;

Analytical::SendRecvTrackingMap AnalyticalNetwork::send_recv_tracking_map;

std::string AnalyticalNetwork::stat_path;

int AnalyticalNetwork::stat_row;

int AnalyticalNetwork::total_stat_rows;

std::shared_ptr<AstraSim::CSVWriter> AnalyticalNetwork::end_to_end_csv;

std::shared_ptr<AstraSim::CSVWriter> AnalyticalNetwork::dimensional_info_csv;

void AnalyticalNetwork::set_event_queue(
    const std::shared_ptr<EventQueue>& event_queue_ptr) noexcept {
  AnalyticalNetwork::event_queue = event_queue_ptr;
}

void AnalyticalNetwork::set_topology(
    const std::shared_ptr<Topology>& topology_ptr) noexcept {
  AnalyticalNetwork::topology = topology_ptr;
}

void AnalyticalNetwork::set_csv_configuration(
    const std::string& stat_path,
    int stat_row,
    int total_stat_rows,
    std::shared_ptr<AstraSim::CSVWriter> end_to_end_csv,
    std::shared_ptr<AstraSim::CSVWriter> dimensional_info_csv) noexcept {
  AnalyticalNetwork::stat_path = stat_path;
  AnalyticalNetwork::stat_row = stat_row;
  AnalyticalNetwork::total_stat_rows = total_stat_rows;
  AnalyticalNetwork::end_to_end_csv = end_to_end_csv;
  AnalyticalNetwork::dimensional_info_csv = dimensional_info_csv;
}

AnalyticalNetwork::AnalyticalNetwork(int rank) noexcept
    : AstraSim::AstraNetworkAPI(rank) {}

int AnalyticalNetwork::sim_comm_size(AstraSim::sim_comm comm, int* size) {
  return 0;
}

int AnalyticalNetwork::sim_finish() {
  return 0;
}

double AnalyticalNetwork::sim_time_resolution() {
  return 0;
}

int AnalyticalNetwork::sim_init(AstraSim::AstraMemoryAPI* MEM) {
  return 0;
}

AstraSim::timespec_t AnalyticalNetwork::sim_get_time() {
  return event_queue->get_current_time();
}

void AnalyticalNetwork::sim_schedule(
    AstraSim::timespec_t delta,
    void (*fun_ptr)(void*),
    void* fun_arg) {
  // 1. compute event_time = current_time + delta
  auto event_time = sim_get_time();

  // FIXME: assuming time_res is always NS
  event_time.time_val += delta.time_val;

  // 2. schedule an event at the event_time
  event_queue->add_event(event_time, fun_ptr, fun_arg);
}

int AnalyticalNetwork::sim_send(
    void* buffer,
    int count,
    int type,
    int dst,
    int tag,
    AstraSim::sim_request* request,
    void (*msg_handler)(void*),
    void* fun_arg) {
  // get source id
  auto src = sim_comm_get_rank();

  // compute send latency in ns    // FIXME: if you want to use time_res other
  // than NS
  AstraSim::timespec_t delta;
  delta.time_res = AstraSim::NS;
  delta.time_val =
      (int)topology->send(src, dst, count); // simulate src->dst and get latency

  if (send_recv_tracking_map.has_recv_operation(tag, src, dst, count)) {
    // recv operation already issued.
    // Schedule both send and recv event handler.
    auto recv_event_handler =
        send_recv_tracking_map.pop_recv_event_handler(tag, src, dst, count);
    sim_schedule(delta, msg_handler, fun_arg);
    sim_schedule(
        delta,
        recv_event_handler.get_fun_ptr(),
        recv_event_handler.get_fun_arg());
  } else {
    // recv operation not issued yet.
    // Should assign this send operation to the tracker.

    // schedule send event
    sim_schedule(delta, msg_handler, fun_arg);

    // compute send finish time  // FIXME: if you want to use time_res other
    // than NS
    auto send_finish_time = sim_get_time();
    send_finish_time.time_val += delta.time_val;

    // schedule this into the tracker
    send_recv_tracking_map.insert_send(tag, src, dst, count, send_finish_time);
  }

  return 0;
}

int AnalyticalNetwork::sim_recv(
    void* buffer,
    int count,
    int type,
    int src,
    int tag,
    AstraSim::sim_request* request,
    void (*msg_handler)(void*),
    void* fun_arg) {
  // get source id
  auto dst = sim_comm_get_rank();

  if (send_recv_tracking_map.has_send_operation(tag, src, dst, count)) {
    // send operation already issued.
    // should compute delta
    AstraSim::timespec_t delta;
    delta.time_res = AstraSim::NS;

    auto current_time = sim_get_time();
    auto send_finish_time =
        send_recv_tracking_map.pop_send_finish_time(tag, src, dst, count);

    if (EventQueueEntry::compare_time_stamp(current_time, send_finish_time) <
        0) {
      // sent packet still inflight
      // schedule recv handler accordingly.
      delta.time_val = send_finish_time.time_val - current_time.time_val;
    } else {
      // send operation already finished.
      // invoke recv handler immediately
      delta.time_val = 0;
    }

    // schedule recv handler
    sim_schedule(delta, msg_handler, fun_arg);
  } else {
    // send operation not issued.
    // Add recv to the tracker and wait until corresponding sim_send to be
    // invoked.
    send_recv_tracking_map.insert_recv(
        tag, src, dst, count, msg_handler, fun_arg);
  }

  return 0;
}

void AnalyticalNetwork::pass_front_end_report(
    AstraSim::AstraSimDataAPI astraSimDataAPI) {
  auto run_name = astraSimDataAPI.run_name;
  auto running_time = std::to_string(astraSimDataAPI.workload_finished_time);
  auto compute_time = std::to_string(astraSimDataAPI.total_compute);
  auto expoed_comm_time = std::to_string(astraSimDataAPI.total_exposed_comm);

  AnalyticalNetwork::end_to_end_csv->write_cell(stat_row + 1, 0, run_name);
  AnalyticalNetwork::end_to_end_csv->write_cell(stat_row + 1, 1, running_time);
  AnalyticalNetwork::end_to_end_csv->write_cell(stat_row + 1, 2, compute_time);
  AnalyticalNetwork::end_to_end_csv->write_cell(
      stat_row + 1, 3, expoed_comm_time);

  auto chunk_latencies =
      astraSimDataAPI.avg_chunk_latency_per_logical_dimension;
  // fixme: assuming max_dimension is 10
  // fixme: dimensions_count for every topology differs
  auto base_index = (stat_row * 10) + 1;
  for (auto i = 0; i < chunk_latencies.size(); i++) {
    auto row_to_write = base_index + i;
    auto dimension_id = std::to_string(i);
    auto chunk_latency = std::to_string(chunk_latencies[i]);

    AnalyticalNetwork::dimensional_info_csv->write_cell(
        row_to_write, 0, run_name);
    AnalyticalNetwork::dimensional_info_csv->write_cell(
        row_to_write, 1, dimension_id);
    AnalyticalNetwork::dimensional_info_csv->write_cell(
        row_to_write, 2, chunk_latency);
  }
}
