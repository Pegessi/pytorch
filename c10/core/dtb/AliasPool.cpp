#include <c10/core/dtb/AliasPool.h>
#include <c10/core/dtb/utils.h>
#include <c10/cuda/dtb/DTBManager.h>

#define TORCH_CHECK(a, ...)   // replace original TORCH_CHECK  profile mode

namespace c10 {
namespace dtb {

/**
 * Methods of AliasPool
 * Implementation about metrics updating and eviction behavior 
*/
#pragma region AliasPoolMethods

// An aliaspool cant register itself to the checkpointpool - you have to do it yourself.
AliasPool::AliasPool(const Unsafe&, intrusive_ptr<Rematerializer> head_remat, size_t memory, int device_id) :
  head_remat(head_remat),
  memory(memory),
  device_id(device_id),
  last_used_time(std::chrono::system_clock::now()) {
}

AliasPool::AliasPool(const Unsafe&, intrusive_ptr<Rematerializer> head_remat, size_t memory, uintptr_t addr, int device_id) :
  head_remat(head_remat),
  memory(memory),
  addr(addr),
  device_id(device_id),
  last_used_time(std::chrono::system_clock::now()) {
}

AliasPool::AliasPool(const Unsafe&, intrusive_ptr<Rematerializer> head_remat, size_t memory, uintptr_t addr, int device_id, bool if_w) :
  head_remat(head_remat),
  memory(memory),
  addr(addr),
  device_id(device_id),
  if_weight(if_w),
  last_used_time(std::chrono::system_clock::now()) {
}

void AliasPool::release_resources() {
  if(!is_evicted){
    evict(2);
  }
  tensors.clear();
  neighbors.clear();
  head_remat.reset();
}

CheckpointInfo merge_cpi(CheckpointInfo l, CheckpointInfo r) {
  STATS.track("merge_cpi");
  return CheckpointInfo(l.compute_cost + r.compute_cost);
}

void AliasPool::evict(int mode) { // 0 - evict | 1 - deconstruct | 2 - Irreversible deconstruction
  STATS.track("AliasPool::evict");
  TORCH_CHECK(!ecn);
  if(mode!=2){
    ecn = head_remat->get_ecn();      /// 发生驱逐|可恢复释放行为，初始化ecn
    auto ecns = neighbor_ecn();
    for (const auto& necn : ecns) {
      merge<CheckpointInfo>(merge_cpi, ecn, necn);
    }
  }
  TORCH_CHECK(memory > 0);
  TORCH_CHECK(lock_count == 0);
  TORCH_CHECK(!is_evicted);
  is_evicted = true;
  for (const weak& w : tensors) {
    if (auto cell = w.lock()) {
#ifdef DEBUG_MODE
      if(record_er_counts){
        if(mode==0)
        {
          tensor_evict_counts += 1;
          // DTRLogEvictEvents(cell->counter_name(), tensor_evict_counts);
        }
        else
          tensor_destruct_counts += 1;
      }
#endif
      printf("evict cell trigger\n");
      cell->evict();
    }
  }
  if(mode==1){  /// memory order use
    auto *pm = getDTBPoolManager();
    pm->erase_ap(device_id, addr);
  }
}

void AliasPool::unlock() {
  --lock_count;   // external == 0 , lock_count > 0 == 0
  /// improvement for life cycle
  /// because that staleness is harmful to eviction of remated tensor during backward progress, which should be released immediately
#ifndef ORIGINAL_DTR
  if(remat_count>0){
    unlock_remated();
  #ifdef DEBUG_MODE
    if(record_lifecycle){ // 这里记录的是重物化过程的情况
      pid_t pid = getpid();
      DTRLogLifeCycle(std::to_string(pid), external_count, lock_count, remat_count);
    }
  #endif
    if(remat_count == 0 && external_count == 0 && lock_count == 0 && retain_count == 0){
      if (memory > 0 && (!ecn) && head_remat) {
        evict(1);
      } 
      // else if (memory > 0 && head_remat==nullptr)
      //   evict(2);
    }
  }
  /**
   * 上面的重物化检查相当于提供了一个释放重物化张量的timing
   * 但实际上由于动态执行中会出现remat_count==0但lock_count>0导致无法回收的情况（错过了这个回收窗口）
   * 因此在反向过程中额外检查是否有释放的机会
   * Plus: 锁定的张量也可能在这里释放
  */
  if(during_backward){
    if(remat_count == 0 && external_count == 0 && lock_count == 0){
      if (memory > 0 && (!ecn) && head_remat) {
        evict(1);
      } 
    }
  }
#endif
}

void AliasPool::release_external() {
  --external_count;
  if (external_count == 0) {          /// TODO: 潜在bug，如果lock_count>0，此后这个aps会成为僵尸内存; 反向内存堆积的原因是否是因为这个？ 还是其他的引用计数
    if(if_weight) return;
    if (lock_count > 0) {
      return;
    }
    

    if (memory > 0 && (!ecn) && head_remat) {   /// [TAG] 对于无源之水无本之木，他们在这里就不会被释放了，包括所有模型权重和其他直接checkpoint的结果
#ifdef DEBUG_MODE
      // DTRLogDestructEvents();
      destruct_counts += 1;
#endif
      evict(1);
    } 
    // else if(memory > 0 && head_remat == nullptr){   /// 配合release_external_of_nosource_tensor才能释放这些张量
    //   evict(2);
    // }
  }
}

int AliasPool::update_dep_task(){
  auto& last_t = tensors.back();
  if(auto cell = last_t.lock()){
    return cell->precheck();
  }
  return 0;
}

void AliasPool::update_dependency() {
  dep_future = std::async(std::launch::async, [this]() { return this->update_dep_task(); });

  // for (size_t i = 0; i < tensors.size(); i++){
  //   {
  //     if(auto cell = tensors[i].lock()){
  //       if(cell->pool->dependency>0){
  //         dependency += cell->pool->dependency;
  //       }else{
  //         cell->precheck(dependency);
  //       }
  //     }
  //   }
  // }
}

double AliasPool::cost(time_t current_time) {
#ifdef ORIGINAL_DTR
  time_t pre = std::chrono::system_clock::now();
#endif
#ifdef TIMER_ENABLE
  time_t pre = std::chrono::system_clock::now();
#endif
  auto cpi = head_remat->get_cpi();
  auto ecns = neighbor_ecn();
  for (const auto& necn : ecns) {
    cpi = merge_cpi(cpi, get_t(necn));
  }
#ifdef DEPENDENCY_CHECK
  // 给依赖深的增加penalty
  auto ret = cpi.cost(memory, (current_time - last_used_time).count() * (1 + get_dependency() * 100));
#else
  auto ret = cpi.cost(memory, (current_time - last_used_time).count());
#endif
#ifdef ORIGINAL_DTR
  time_t post = std::chrono::system_clock::now();
  cost_time_ += (post - pre).count();
#endif
#ifdef TIMER_ENABLE
  time_t post = std::chrono::system_clock::now();
  cost_time_ += (post - pre).count();
#endif
  return ret;
}

std::set<ecn_ptr> AliasPool::neighbor_ecn() {
  STATS.track("AliasPool::neighbor_ecn");
  std::set<ecn_ptr> ptr_set;
  int size = neighbors.size();
  for (size_t i = 0; i < size;) {
    if (auto cptc = neighbors[i].lock()) {
      if (cptc->pool->ecn) {
        ptr_set.insert(cptc->pool->ecn);
      }
      ++i;
    } else {
      neighbors[i] = neighbors[size - 1];
      size = size - 1;
    }
  }
  if (size < neighbors.size()) {
    neighbors.erase(neighbors.begin() + size);
  }
  return ptr_set;
}

void AliasPool::set_not_evicted(const intrusive_ptr<AliasPool>& self) {
  if (likely(is_evicted)) {
    STATS.track("AliasPool::set_not_evicted(inside)");
    is_evicted = false;
    if (ecn) {
      TORCH_CHECK(head_remat);
      auto cpi = get_t(ecn);
      update_t(ecn, CheckpointInfo(cpi.compute_cost - head_remat->compute_cost));
      ecn.reset();
    }

#ifdef MULTI_MODE
    auto *pm = getDTBPoolManager();
    pm->add_ap(device_id, self);
#else
    pool.add(self);
#endif
  }
}

#pragma endregion


    
}
}