#include "renderer_query_clock.h"
#include "renderer_fence_wait.h"
#include <iostream>
#include <thread>
#include <chrono>
using Microsoft::WRL::ComPtr;
int main(){
  if(outrun::QueryClockMarker::Delta(2,5)!=-3 || outrun::QueryClockMarker::Delta(5,2)!=3)return 1;
  ComPtr<ID3D12Device> device;
  if(FAILED(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device))))return 77;
  D3D12_COMMAND_QUEUE_DESC desc{};desc.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
  ComPtr<ID3D12CommandQueue> queue;ComPtr<ID3D12CommandAllocator> allocator;
  ComPtr<ID3D12GraphicsCommandList> list;ComPtr<ID3D12Fence> fence;
  if(FAILED(device->CreateCommandQueue(&desc,IID_PPV_ARGS(&queue))) ||
     FAILED(device->CreateCommandAllocator(desc.Type,IID_PPV_ARGS(&allocator))) ||
     FAILED(device->CreateCommandList(0,desc.Type,allocator.Get(),nullptr,IID_PPV_ARGS(&list))) ||
     FAILED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence))))return 2;
  outrun::QueryClockMarker marker;
  if(!marker.Prepare(device.Get(),queue.Get()))return 77;
  list->EndQuery(marker.heap.Get(),D3D12_QUERY_TYPE_TIMESTAMP,0);
  list->ResolveQueryData(marker.heap.Get(),D3D12_QUERY_TYPE_TIMESTAMP,0,1,marker.readback.Get(),0);
  if(FAILED(list->Close()))return 3;
  ID3D12CommandList* commands=list.Get();queue->ExecuteCommandLists(1,&commands);
  if(FAILED(queue->Signal(fence.Get(),1)))return 4;
  try{outrun::WaitForQueryFence(fence.Get(),1);}catch(...){return 5;}
  const auto result=marker.Read(queue.Get(),outrun::QueryClockMarker::CpuNow());
  std::cout<<"recorded_to_gpu_ms="<<result.recorded_to_gpu_ms<<" tail_to_wake_ms="
    <<result.gpu_tail_to_wake_ms<<" drift_ms="<<result.calibration_drift_ms<<" valid="<<result.valid<<'\n';
  if(!marker.mapped || !*marker.mapped)return 6;
  // Completed GPU work cannot move; a known CPU delay must appear on the
  // tail-to-wake side, not as additional GPU queue time.
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  const auto delayed=marker.Read(queue.Get(),outrun::QueryClockMarker::CpuNow());
  if(result.valid && delayed.valid &&
     (delayed.gpu_tail_to_wake_ms-result.gpu_tail_to_wake_ms<15.0 ||
      std::abs(delayed.recorded_to_gpu_ms-result.recorded_to_gpu_ms)>0.001))return 7;
  std::cout<<"Known CPU delay tail increase_ms="
    <<delayed.gpu_tail_to_wake_ms-result.gpu_tail_to_wake_ms<<'\n';
  // Correlation validity is hardware-dependent; timestamp delivery must work.
  return 0;
}
