"""Compile the actual SDK fence condition against controlled HRESULT results.

No GPU/window is created. Checks event-registration ordering and prevents
entering an infinite fence wait when Signal or SetEventOnCompletion failed.
"""
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
source = root/'out/dependencies/rexglue-sdk/src/graphics/d3d12/command_processor.cpp'
text = source.read_text()
start = text.index('      if (SUCCEEDED(direct_queue->Signal(')
end = text.index(' {', start)
condition = text[start:end].strip()[4:-1]  # contents of the actual if (...)
out = root/'out/fence-regression-20261026'
out.mkdir(exist_ok=True)
fixture = r'''
#include <cstdint>
#include <cstdio>
using HRESULT = int32_t;
#define SUCCEEDED(hr) (((HRESULT)(hr)) >= 0)
struct Fence {
  HRESULT result; int calls = 0;
  HRESULT SetEventOnCompletion(uint64_t, void*) { ++calls; return result; }
};
struct Queue {
  HRESULT result; int calls = 0;
  HRESULT Signal(Fence*, uint64_t) { ++calls; return result; }
};
int main() {
  int errors = 0;
  for (HRESULT signal : {HRESULT(0), HRESULT(1), HRESULT(-1)}) {
    for (HRESULT registration : {HRESULT(0), HRESULT(-1)}) {
      Queue queue{signal}; Fence fence{registration};
      auto* direct_queue = &queue;
      auto* queue_operations_since_submission_fence_ = &fence;
      uint64_t fence_value = 42; void* fence_completion_event_ = nullptr;
      bool enter_wait = CONDITION;
      bool valid = queue.calls == 1 && fence.calls == (signal >= 0 ? 1 : 0)
                   && enter_wait == (signal >= 0 && registration >= 0);
      std::printf("signal=%d registration=%d event_calls=%d wait=%d: %s\n",
                  signal, registration, fence.calls, enter_wait, valid ? "PASS" : "FAIL");
      errors += !valid;
    }
  }
  return errors ? 1 : 0;
}
'''.replace('CONDITION',condition).replace('#include <cstdint>','#include <cstdint>\n#include <initializer_list>')
(out/'fence_condition.cpp').write_text(fixture)
subprocess.run(['clang++','-std=c++20',str(out/'fence_condition.cpp'),'-o',str(out/'fence_condition.exe')],check=True)
result = subprocess.run([str(out/'fence_condition.exe')],capture_output=True,text=True)
print(result.stdout)
(out/('result-passed.txt' if result.returncode == 0 else 'result-failed.txt')).write_text(result.stdout+result.stderr)
sys.exit(result.returncode)
