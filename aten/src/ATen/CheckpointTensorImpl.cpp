/*
  文件开头的宏定义是用于不同优化或者debug模式进行开关的
*/
#pragma once

#include <ATen/CheckpointTensorImpl.h>
// #include <c10/core/CheckpointTensorImpl.h>
// #include <ATen/Logger.h>
// #include <c10/cuda/CUDACachingAllocator.h>
#include <c10/cuda/dtb/DTBManager.h>
#include <cuda_runtime_api.h>
#include <chrono>
#include <string>
#include <random>
#include <cmath>
#include <queue>
#include <unordered_map>
#include <unistd.h>


namespace c10 {
namespace dtb {

// using Clock = std::chrono::high_resolution_clock;
// using Time = Clock::time_point;
// using Duration = Clock::duration;
// using FinalTime = std::chrono::nanoseconds;
// const time_t test_time_post = std::chrono::system_clock::now();
// const time_t test_time_cur = std::chrono::system_clock::now();
// auto test_dur = test_time_cur - test_time_post;


// /* 这几项用于记录tensor内存分布特征 */
// size_t memory_sum = 0;
// size_t memory_max = 0;
// size_t memory_count = 0;


// long base_compute_time_ = 0;
// long remat_compute_time_ = 0;
// long search_time_ = 0;
// long cost_time_ = 0;
// bool use_log_ = false;
// bool use_profile_ = false;
// #ifdef DEBUG_MODE
// bool record_er_counts = false;        // 驱逐&重物化次数
// bool record_mem_addr = true;         // 是否记录内存地址
// bool record_op_recs = false;          // 是否记录op历史
// bool record_fragmentation = false;    // 记录碎片化和内存占用数据
// bool record_lifecycle = false;        // 记录ap生命周期计数分布
// bool record_ap_cost = false;          // 记录ap的cost分布
// bool record_dependcy = false;
// bool record_key_chain = false;
// bool current_if_any_evicted = false;

// std::atomic<size_t> evict_counts = 0;
// std::atomic<size_t> tensor_evict_counts = 0;
// std::atomic<size_t> remat_counts = 0;
// std::atomic<size_t> cannot_evict_counts = 0;
// std::atomic<size_t> destruct_counts = 0;
// std::atomic<size_t> tensor_destruct_counts = 0;

// void signal_handler(int sig) {
//   constexpr const int REC_DEPTH = 50;
//   void *array[REC_DEPTH];
//   size_t size;
//   char **strings;
//   size_t i;

//   // 获取当前的调用栈
//   size = backtrace(array, REC_DEPTH);
//   strings = backtrace_symbols(array, size);

//   fprintf(stderr, "Error: signal %d:\n", sig);
//   for (i = 0; i < size; i++) {
//       fprintf(stderr, "%s\n", strings[i]);
//   }

//   free(strings);
//   exit(1);
// }

// #endif

// // bool reserved_range = false;
// // bool during_backward = false;
// // // bool if_train_mode = false;
// // using at::reserved_range;
// // using at::during_backward;


// void reset_memory_stat() {
//   memory_sum = 0;
//   memory_max = 0;
//   memory_count = 0;
// }


}
}


namespace at {


// using c10::dtb::STATS;
using c10::dtb::getDTBPoolManager;
using c10::dtb::use_log_;
using c10::dtb::base_compute_time_;
using c10::dtb::remat_compute_time_;
using c10::dtb::search_time_;
using c10::dtb::cost_time_;
using c10::dtb::use_profile_;

using c10::dtb::base_compute_time_;
using c10::dtb::remat_compute_time_;
using c10::dtb::search_time_;
using c10::dtb::cost_time_;
using c10::dtb::use_log_;
using c10::dtb::use_profile_;
#ifdef DEBUG_MODE
using c10::dtb::record_er_counts;        // 驱逐&重物化次数
using c10::dtb::record_mem_addr;         // 是否记录内存地址
using c10::dtb::record_op_recs;          // 是否记录op历史
using c10::dtb::record_fragmentation;    // 记录碎片化和内存占用数据
using c10::dtb::record_lifecycle;        // 记录ap生命周期计数分布
using c10::dtb::record_ap_cost;          // 记录ap的cost分布
using c10::dtb::record_dependcy;
using c10::dtb::record_key_chain;
using c10::dtb::current_if_any_evicted;

using c10::dtb::evict_counts;
using c10::dtb::tensor_evict_counts;
using c10::dtb::remat_counts;
using c10::dtb::cannot_evict_counts;
using c10::dtb::destruct_counts;
using c10::dtb::tensor_destruct_counts;

#endif
/**
 * Interface expose to at native
 * Kinds of log methods
 * Initiative methods in Aten
*/
#pragma region InterfaceForAtenNative

namespace native {

/**
 * This function is used to transfer the original tensor created by torch
 * and attain the ability to manage their lifecyle.
 * 
 * @param t: original tensor
 * @param if_weight: if this tensor is a weight tensor or you do not wish it to be destroyed
 * 
 * @return res: tensor returned is a "empty" tensor without storage, which is moved into cpti
 * 
 * 
 * @skip
 * ---------------------------------------some debug notes-------------------------------------
 * !TODO: 潜在问题 引入张量t有undefined的情况，此种情况cpti构造出来的tensor却不是undefined的了，可以额外标记tensor是undefined
 * 需要规避空tensor带来的可能问题，调用empty tensor的成员方法会报错
 * [TAG] if_weight的标记操作只是保证了生命周期的合理性，即权重不释放，非权重的无源tensor释放
*/
Tensor checkpoint(Tensor& t, bool if_weight) {
  // STATS.track("checkpoint");
  // if(!t.defined())
  //   return Tensor(nullptr);
  // auto cpti = intrusive_ptr<CheckpointTensorImpl>::make(t);   // 调用了Ref<intrusive_ptr<External>> External CheckpointTensorCell的相关构造函数
  auto cpti = c10::make_intrusive<CheckpointTensorImpl>(t, if_weight);      // cpti->ref->value->value->t 是包裹的unique_ptr<Tensor> unsafeGetTensorCell()
#ifdef DEBUG_MODE
  if (use_log_) {
    c10::dtb::DTRLogConstant(cpti->counter_name());
    c10::dtb::DTRLogMemory(std::to_string(cpti->unsafeGetTensorCell()->pool->if_weight)+"-"+std::to_string(if_weight), cpti->unsafeGetTensorCell()->memory());
  }
#endif
  auto res = Tensor(cpti);
  return res;
}

Tensor uncheckpoint(const Tensor& t) {
  // STATS.track("uncheckpoint");
  auto* cpti = dynamic_cast<CheckpointTensorImpl*>(t.unsafeGetTensorImpl());
  TORCH_CHECK(cpti != nullptr);
  return cpti->unsafeGetTensorCell()->get();
}

/**
 * 支持上层操作的下层实现接口
*/
void pin(Tensor& t) {
  // STATS.track("pin");
  auto* cpti = dynamic_cast<CheckpointTensorImpl*>(t.unsafeGetTensorImpl());
  TORCH_CHECK(cpti != nullptr);
  cpti->unsafeGetTensorCell()->pin();
}

/**
 * Decheckpoint will create a shallow copy(detach) of tensor in cptc, which shares the same memory with cptc.
 * It can be used in those scenario where progress is not managed by DTR runtime like custom kernel and communication progress.
 * 
 * @param t: tensor self
 * @param if_comm: mark if this tensor is decheckpointed for communication
 * 
 * @return res: if t is a cptc, return inner t.detach(), otherwise return t self 
 * 
 * @skip
 * ---------------------------------------some debug notes-------------------------------------
 * 
*/
Tensor decheckpoint(const Tensor& t, bool if_comm) {
  // STATS.track("decheckpoint");
  auto* cpti = dynamic_cast<CheckpointTensorImpl*>(t.unsafeGetTensorImpl());
  if(cpti){
    if (if_comm) cpti->unsafeGetTensorCell()->pool->is_retain = true;
    auto res = cpti->unsafeGetTensorCell()->get();
    // return res;    // BUG: segmentation fault
    return res.detach();
  }else
    return t;
  // return cpti ? cpti->unsafeGetTensorCell()->get() : t;
}

void cpti_decrease(const Tensor& t) {
  // STATS.track("decheckpoint");
  auto* cpti = dynamic_cast<CheckpointTensorImpl*>(t.unsafeGetTensorImpl());
  if(cpti) cpti->release_resources();
}

bool is_checkpoint(const Tensor& t) {
  // STATS.track("is_checkpoint");
  auto* cpti = dynamic_cast<CheckpointTensorImpl*>(t.unsafeGetTensorImpl());
  return cpti != nullptr;
}

Tensor try_checkpoint(Tensor& t) {
  // STATS.track("try_checkpiont");
  // if(t.key_set().has(DispatchKey::Checkpoint)&&!is_checkpoint(t)){   /// 这行代码不一定有用 但debug验证的代价较大
  //   // t.key_set() = t.key_set().remove(DispatchKey::Checkpoint);    // 返回值是keyset，但没有set函数 所以是没有用的
  //   annotate_log("trigger");
  //   return(checkpoint(t.decheckpoint()));
  // }
  return is_checkpoint(t) ? t : checkpoint(t);
}

void new_log(std::string str) {
  c10::dtb::DTRLogger::logger().out = std::ofstream(c10::dtb::DTRLogger::logger().get_filename(str));
}

void annotate_log(c10::string_view str_) {
  auto str = std::string(str_);
  if (!use_log_) { return; }
  if (c10::dtb::log_json) {
    c10::dtb::json j;
    j["INSTRUCTION"] = "ANNOTATE";
    j["ANNOTATION"] = str;
    c10::dtb::DTRLogger::logger().log(j.dump());
  } else {
    c10::dtb::DTRLogger::logger().log("# " + str);
  }
}

void toggle_log(bool b) {
  use_log_ = b;
}

void clear_checkpointpool(long device, bool last_iter) {
#ifdef MULTI_MODE
  auto *pm = getDTBPoolManager();
  pm->clear_checkpointpool(device, last_iter);
#else
  while (likely(!pool.exts.empty())) {
    if (auto e = pool.exts.back().lock()) {
      e->value->pin();
    }
    pool.exts.pop_back();
  }
#endif
}

void check_current_exts(long device){
  auto *pm = getDTBPoolManager();
  pm->pool_cur_mem_snapshot(device);
}

void init_dtb_manager(){
#ifdef MULTI_MODE
  c10::dtb::lazyInitDTB();
#endif
}

void unset_memory_budget() {
#ifdef MULTI_MODE
  auto *pm = getDTBPoolManager();
  pm->unset_memory_budget();
#else
  pool.has_memory_budget = false;
#endif
}

void set_memory_budget(long budget) {
#ifdef MULTI_MODE
  auto *pm = getDTBPoolManager();
  pm->set_memory_budget(budget);
  c10::dtb::set_global_memory_budget(budget);
#else
  pool.memory_budget = budget;
  pool.has_memory_budget = true;
#endif
}

void set_reserved(){
  reserved_range = true;
}

void unset_reserved(){
  reserved_range = false;
}

void set_backward_flag(){
// #ifdef MULTI_MODE
//   auto *pm = getDTBPoolManager();
//   pm->set_during_backward(true);
// #else
  during_backward = true;
// #endif
  // printf("SET_BACKWARD_FALG TRIGGER\n");
}

void unset_backward_flag(){
// #ifdef MULTI_MODE
//   auto *pm = getDTBPoolManager();
//   pm->set_during_backward(false);
// #else
  during_backward = false;
// #endif
  // printf("UNSET_BACKWARD_FALG TRIGGER\n");
}

void mark_train(bool flag){
#ifdef MULTI_MODE
  auto *pm = getDTBPoolManager();
  pm->set_train_mode(flag);
#else
  if_train_mode = flag;
#endif
}

/// TODO: use dtb, useless here
void force_evict(long mode){
#ifdef MULTI_MODE
  auto *pm = getDTBPoolManager();
  pm->force_evict(0, mode);
#else
  pool.force_evict(mode);
#endif
}

void log_dtr_statics(){
#ifdef DEBUG_MODE
#ifdef MULTI_MODE
  if(record_fragmentation){
    auto *pm = getDTBPoolManager();
    int did = 0;
    for(const auto& mem_info: pm->get_peak_memory()){
      std::stringstream log_str;
      log_str << "device-" << did << " peak allocated memory";
      c10::dtb::DTRLogCounts(log_str.str(), mem_info.first);
      log_str.clear();
      log_str << "device-" << did << " peak reserved memory";
      c10::dtb::DTRLogCounts(log_str.str(), mem_info.second);
      log_str.clear();
      log_str << "device-" << did << " fragmentation ratio";
      c10::dtb::DTRLogCounts(log_str.str(), static_cast<double>(mem_info.first) / static_cast<double>(mem_info.second));
      did++;
    }
  }
#endif
  if(record_er_counts){
    // DTRLogCounts("memory budget", memo);
    c10::dtb::DTRLogCounts("evict counts", evict_counts);
    c10::dtb::DTRLogCounts("evict tensor counts", tensor_evict_counts);
    c10::dtb::DTRLogCounts("cannot evict counts", cannot_evict_counts);
    c10::dtb::DTRLogCounts("destruct counts", destruct_counts);
    c10::dtb::DTRLogCounts("destruct tensor counts", tensor_destruct_counts);
    c10::dtb::DTRLogCounts("remat counts", remat_counts);
  }
#endif
}

/// TODO: use dtb
void toggle_sampling(bool sample) {
#ifdef MULTI_MODE
  auto *pm = getDTBPoolManager();
  pm->toggle_sampling(sample);
#else
  pool.sample_tensors = sample;
#endif
}

/// TODO: as pool member function
void toggle_ignore_small_tensors(bool ignore) {
#ifdef MULTI_MODE
  auto *pm = getDTBPoolManager();
  pm->toggle_ignore_small_tensors(ignore);
#else
  pool.ignore_small_tensors = ignore;
#endif
}

void reset_profile() {
  base_compute_time_ = 0;
  remat_compute_time_ = 0;
  search_time_ = 0;
  cost_time_ = 0;
}

void toggle_profile(bool profile) {
  use_profile_ = profile;
}

long compute_time() {
  return base_compute_time() + remat_compute_time();
}

long cost_time() {
  return cost_time_;
}

long search_time() {
  return search_time_;
}

long remat_compute_time() {
  return remat_compute_time_;
}

long base_compute_time() {
  return base_compute_time_;
}

long loop_time() {
  return search_time() - cost_time();
}

}
#pragma endregion
}

