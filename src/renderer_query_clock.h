#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <cstdint>
#include <cmath>

namespace outrun {
// Diagnostic only. One timestamp at the tail of the query-result submission.
// Reuse only after its worker has observed the submission fence completing.
struct QueryClockMarker {
  Microsoft::WRL::ComPtr<ID3D12QueryHeap> heap;
  Microsoft::WRL::ComPtr<ID3D12Resource> readback;
  uint64_t* mapped=nullptr;
  uint64_t gpu_frequency=0,cpu_frequency=0,before_gpu=0,before_cpu=0,record_cpu=0;
  ~QueryClockMarker(){if(mapped)readback->Unmap(0,nullptr);}
  static uint64_t CpuNow(){LARGE_INTEGER v;QueryPerformanceCounter(&v);return uint64_t(v.QuadPart);}
  static double Delta(uint64_t a,uint64_t b){return a>=b?double(a-b):-double(b-a);}
  bool Prepare(ID3D12Device* device,ID3D12CommandQueue* queue) {
    if(!heap) {
      D3D12_QUERY_HEAP_DESC h{};h.Type=D3D12_QUERY_HEAP_TYPE_TIMESTAMP;h.Count=1;
      if(FAILED(device->CreateQueryHeap(&h,IID_PPV_ARGS(heap.ReleaseAndGetAddressOf()))))return false;
      D3D12_HEAP_PROPERTIES properties{};properties.Type=D3D12_HEAP_TYPE_READBACK;
      D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;
      desc.Width=8;desc.Height=1;desc.DepthOrArraySize=1;desc.MipLevels=1;
      desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
      if(FAILED(device->CreateCommittedResource(&properties,D3D12_HEAP_FLAG_NONE,&desc,
          D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(readback.ReleaseAndGetAddressOf())))) {
        heap.Reset();return false;
      }
      D3D12_RANGE range{0,8};void* data=nullptr;
      if(FAILED(readback->Map(0,&range,&data))){heap.Reset();readback.Reset();return false;}
      mapped=static_cast<uint64_t*>(data);
    }
    LARGE_INTEGER cpu;QueryPerformanceFrequency(&cpu);cpu_frequency=uint64_t(cpu.QuadPart);
    if(FAILED(queue->GetTimestampFrequency(&gpu_frequency)) || !gpu_frequency || !cpu_frequency ||
       FAILED(queue->GetClockCalibration(&before_gpu,&before_cpu)))return false;
    record_cpu=CpuNow();return true;
  }
  template<class Commands> void Record(Commands& commands) {
    commands.D3DEndQuery(heap.Get(),D3D12_QUERY_TYPE_TIMESTAMP,0);
    commands.D3DResolveQueryData(heap.Get(),D3D12_QUERY_TYPE_TIMESTAMP,0,1,readback.Get(),0);
  }
  struct Result {
    bool valid=false;
    double recorded_to_gpu_ms=0,gpu_tail_to_wake_ms=0,calibration_drift_ms=0;
  };
  Result Read(ID3D12CommandQueue* queue,uint64_t wake_cpu) const {
    Result r;uint64_t after_gpu=0,after_cpu=0;
    if(!mapped || FAILED(queue->GetClockCalibration(&after_gpu,&after_cpu)))return r;
    const double ratio=double(cpu_frequency)/double(gpu_frequency);
    const double gpu_relative_cpu=Delta(*mapped,before_gpu)*ratio;
    r.recorded_to_gpu_ms=(Delta(before_cpu,record_cpu)+gpu_relative_cpu)*1000.0/cpu_frequency;
    r.gpu_tail_to_wake_ms=(Delta(wake_cpu,before_cpu)-gpu_relative_cpu)*1000.0/cpu_frequency;
    r.calibration_drift_ms=(Delta(after_gpu,before_gpu)*ratio-Delta(after_cpu,before_cpu))*1000.0/cpu_frequency;
    // Idle GPU clock drift can invalidate correlations. Keep raw values for
    // inspection, and don't mistake them for measured hardware execution time.
    r.valid=std::isfinite(r.calibration_drift_ms) && std::abs(r.calibration_drift_ms)<1.0 &&
            r.recorded_to_gpu_ms>=-0.25 && r.gpu_tail_to_wake_ms>=-0.25;
    return r;
  }
};
}
