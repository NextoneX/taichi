#include "gtest/gtest.h"
#include "taichi/ir/ir_builder.h"
#include "taichi/ir/statements.h"
#include "taichi/inc/constants.h"
#include "taichi/program/program.h"
#include "tests/cpp/ir/ndarray_kernel.h"
#include "tests/cpp/program/test_program.h"
#include "taichi/aot/graph_data.h"
#include "taichi/program/graph_builder.h"
#include "taichi/runtime/gfx/aot_module_loader_impl.h"
#include "taichi/rhi/device.h"

#include "taichi/program/kernel_profiler.h"
#include "taichi/runtime/program_impls/llvm/llvm_program.h"
#include "taichi/system/memory_pool.h"
#include "taichi/runtime/cpu/aot_module_loader_impl.h"
#include "taichi/runtime/cuda/aot_module_loader_impl.h"
#include "taichi/rhi/cuda/cuda_driver.h"
#include "taichi/platform/cuda/detect_cuda.h"

#define TI_RUNTIME_HOST
#include "taichi/program/context.h"
#undef TI_RUNTIME_HOST

using namespace taichi;
using namespace lang;

constexpr int NR_PARTICLES = 8192;
constexpr int N_GRID = 128;

TEST(LlvmCGraph, Mpm88Cpu) {
  CompileConfig cfg;
  cfg.arch = Arch::x64;
  cfg.kernel_profiler = false;
  constexpr KernelProfilerBase *kNoProfiler = nullptr;
  LlvmProgramImpl prog{cfg, kNoProfiler};
  auto *compute_device = prog.get_compute_device();
  // Must have handled all the arch fallback logic by this point.
  auto memory_pool = std::make_unique<MemoryPool>(cfg.arch, compute_device);
  prog.initialize_host();
  uint64 *result_buffer{nullptr};
  prog.materialize_runtime(memory_pool.get(), kNoProfiler, &result_buffer);

  /* AOTLoader */
  cpu::AotModuleParams aot_params;
  const auto folder_dir = getenv("TAICHI_AOT_FOLDER_PATH");

  std::stringstream aot_mod_ss;
  aot_mod_ss << folder_dir;
  aot_params.module_path = aot_mod_ss.str();
  aot_params.executor_ = prog.get_runtime_executor();
  auto mod = cpu::make_aot_module(aot_params);

  // Prepare & Run "init" Graph
  auto g_init = mod->get_graph("init");

  /* Prepare arguments */
  constexpr int kArrBytes_x = NR_PARTICLES * 2 * sizeof(float);
  auto devalloc_x = prog.allocate_memory_ndarray(kArrBytes_x, result_buffer);
  auto x = taichi::lang::Ndarray(devalloc_x, taichi::lang::PrimitiveType::f32,
                                 {NR_PARTICLES}, {2});

  constexpr int kArrBytes_v = NR_PARTICLES * 2 * sizeof(float);
  auto devalloc_v = prog.allocate_memory_ndarray(kArrBytes_v, result_buffer);
  auto v = taichi::lang::Ndarray(devalloc_v, taichi::lang::PrimitiveType::f32,
                                 {NR_PARTICLES}, {2});

  constexpr int kArrBytes_J = NR_PARTICLES * sizeof(float);
  auto devalloc_J = prog.allocate_memory_ndarray(kArrBytes_J, result_buffer);
  auto J = taichi::lang::Ndarray(devalloc_J, taichi::lang::PrimitiveType::f32,
                                 {NR_PARTICLES});

  std::unordered_map<std::string, taichi::lang::aot::IValue> args;
  args.insert({"x", taichi::lang::aot::IValue::create(x)});
  args.insert({"v", taichi::lang::aot::IValue::create(v)});
  args.insert({"J", taichi::lang::aot::IValue::create(J)});

  g_init->run(args);
  prog.synchronize();

  // Prepare & Run "update" Graph
  auto g_update = mod->get_graph("update");

  constexpr int kArrBytes_grid_v = N_GRID * N_GRID * 2 * sizeof(float);
  auto devalloc_grid_v =
      prog.allocate_memory_ndarray(kArrBytes_grid_v, result_buffer);
  auto grid_v = taichi::lang::Ndarray(
      devalloc_grid_v, taichi::lang::PrimitiveType::f32, {N_GRID, N_GRID}, {2});

  constexpr int kArrBytes_grid_m = N_GRID * N_GRID * sizeof(float);
  auto devalloc_grid_m =
      prog.allocate_memory_ndarray(kArrBytes_grid_m, result_buffer);
  auto grid_m = taichi::lang::Ndarray(
      devalloc_grid_m, taichi::lang::PrimitiveType::f32, {N_GRID, N_GRID});

  constexpr int kArrBytes_pos = NR_PARTICLES * 3 * sizeof(float);
  auto devalloc_pos =
      prog.allocate_memory_ndarray(kArrBytes_pos, result_buffer);
  auto pos = taichi::lang::Ndarray(
      devalloc_pos, taichi::lang::PrimitiveType::f32, {NR_PARTICLES}, {3});

  constexpr int kArrBytes_C = NR_PARTICLES * sizeof(float) * 2 * 2;
  auto devalloc_C = prog.allocate_memory_ndarray(kArrBytes_C, result_buffer);
  auto C = taichi::lang::Ndarray(devalloc_C, taichi::lang::PrimitiveType::f32,
                                 {NR_PARTICLES}, {2, 2});

  args.insert({"C", taichi::lang::aot::IValue::create(C)});
  args.insert({"grid_v", taichi::lang::aot::IValue::create(grid_v)});
  args.insert({"grid_m", taichi::lang::aot::IValue::create(grid_m)});
  args.insert({"pos", taichi::lang::aot::IValue::create(pos)});

  g_update->run(args);
  prog.synchronize();
}

TEST(LlvmCGraph, Mpm88Cuda) {
  if (is_cuda_api_available()) {
    CompileConfig cfg;
    cfg.arch = Arch::cuda;
    cfg.kernel_profiler = false;
    constexpr KernelProfilerBase *kNoProfiler = nullptr;
    LlvmProgramImpl prog{cfg, kNoProfiler};
    prog.initialize_host();
    uint64 *result_buffer{nullptr};
    prog.materialize_runtime(nullptr, kNoProfiler, &result_buffer);

    /* AOTLoader */
    cuda::AotModuleParams aot_params;
    const auto folder_dir = getenv("TAICHI_AOT_FOLDER_PATH");

    std::stringstream aot_mod_ss;
    aot_mod_ss << folder_dir;
    aot_params.module_path = aot_mod_ss.str();
    aot_params.executor_ = prog.get_runtime_executor();
    auto mod = cuda::make_aot_module(aot_params);

    // Prepare & Run "init" Graph
    auto g_init = mod->get_graph("init");

    /* Prepare arguments */
    constexpr int kArrBytes_x = NR_PARTICLES * 2 * sizeof(float);
    auto devalloc_x = prog.allocate_memory_ndarray(kArrBytes_x, result_buffer);
    auto x = taichi::lang::Ndarray(devalloc_x, taichi::lang::PrimitiveType::f32,
                                   {NR_PARTICLES}, {2});

    constexpr int kArrBytes_v = NR_PARTICLES * 2 * sizeof(float);
    auto devalloc_v = prog.allocate_memory_ndarray(kArrBytes_v, result_buffer);
    auto v = taichi::lang::Ndarray(devalloc_v, taichi::lang::PrimitiveType::f32,
                                   {NR_PARTICLES}, {2});

    constexpr int kArrBytes_J = NR_PARTICLES * sizeof(float);
    auto devalloc_J = prog.allocate_memory_ndarray(kArrBytes_J, result_buffer);
    auto J = taichi::lang::Ndarray(devalloc_J, taichi::lang::PrimitiveType::f32,
                                   {NR_PARTICLES});

    std::unordered_map<std::string, taichi::lang::aot::IValue> args;
    args.insert({"x", taichi::lang::aot::IValue::create(x)});
    args.insert({"v", taichi::lang::aot::IValue::create(v)});
    args.insert({"J", taichi::lang::aot::IValue::create(J)});

    g_init->run(args);
    prog.synchronize();

    // Prepare & Run "update" Graph
    auto g_update = mod->get_graph("update");

    constexpr int kArrBytes_grid_v = N_GRID * N_GRID * 2 * sizeof(float);
    auto devalloc_grid_v =
        prog.allocate_memory_ndarray(kArrBytes_grid_v, result_buffer);
    auto grid_v =
        taichi::lang::Ndarray(devalloc_grid_v, taichi::lang::PrimitiveType::f32,
                              {N_GRID, N_GRID}, {2});

    constexpr int kArrBytes_grid_m = N_GRID * N_GRID * sizeof(float);
    auto devalloc_grid_m =
        prog.allocate_memory_ndarray(kArrBytes_grid_m, result_buffer);
    auto grid_m = taichi::lang::Ndarray(
        devalloc_grid_m, taichi::lang::PrimitiveType::f32, {N_GRID, N_GRID});

    constexpr int kArrBytes_pos = NR_PARTICLES * 3 * sizeof(float);
    auto devalloc_pos =
        prog.allocate_memory_ndarray(kArrBytes_pos, result_buffer);
    auto pos = taichi::lang::Ndarray(
        devalloc_pos, taichi::lang::PrimitiveType::f32, {NR_PARTICLES}, {3});

    constexpr int kArrBytes_C = NR_PARTICLES * sizeof(float) * 2 * 2;
    auto devalloc_C = prog.allocate_memory_ndarray(kArrBytes_C, result_buffer);
    auto C = taichi::lang::Ndarray(devalloc_C, taichi::lang::PrimitiveType::f32,
                                   {NR_PARTICLES}, {2, 2});

    args.insert({"C", taichi::lang::aot::IValue::create(C)});
    args.insert({"grid_v", taichi::lang::aot::IValue::create(grid_v)});
    args.insert({"grid_m", taichi::lang::aot::IValue::create(grid_m)});
    args.insert({"pos", taichi::lang::aot::IValue::create(pos)});

    g_update->run(args);
    prog.synchronize();
  }
}