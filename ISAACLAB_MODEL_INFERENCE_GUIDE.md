# Isaac Lab 模型推理指南：为什么必须使用 exported/policy.pt

## 🔍 问题本质

在使用 Isaac Lab (RSL_RL) 训练的模型进行推理时，**必须使用 `exported/policy.pt` 而不是 `model_XXXX.pt`**。这是因为两种文件的格式和内容完全不同。

## 📦 两种文件格式对比

### 1. 训练检查点文件 (`model_23599.pt`)

**文件位置示例：**
```
robot_lab/logs/rsl_rl/unitree_go2_rough/2025-12-24_16-11-25/model_23599.pt
```

**文件内容：**
```python
{
    'model_state_dict': {
        'std': tensor(...),
        'actor.0.weight': tensor(...),
        'actor.0.bias': tensor(...),
        'actor.2.weight': tensor(...),
        'actor.2.bias': tensor(...),
        # ... 更多参数 (共17个参数)
        'critic.0.weight': tensor(...),
        'critic.0.bias': tensor(...),
        # ...
    },
    'optimizer_state_dict': {...},
    'iter': 23599,
    'infos': {...}
}
```

**特点：**
- ✅ 包含完整的训练状态（模型权重、优化器状态、迭代次数等）
- ❌ **不包含模型结构定义（forward 函数）**
- ❌ **只是参数字典（state_dict），不是可执行模型**
- ❌ **无法直接用于 C++ 推理**
- ✅ 用于继续训练或恢复训练状态

### 2. 导出的 TorchScript 模型 (`exported/policy.pt`)

**文件位置示例：**
```
robot_lab/logs/rsl_rl/unitree_go2_rough/2025-12-24_16-11-25/exported/policy.pt
```

**文件内容：**
```python
# TorchScript 序列化模型
class RecursiveScriptModule:
    def forward(self, x: Tensor) -> Tensor:
        actor = self.actor
        normalizer = self.normalizer
        _0 = (actor).forward((normalizer).forward(x, ), )
        return _0
    
    # 包含完整的模型结构和参数
    actor: Sequential(...)
    normalizer: Module(...)
```

**特点：**
- ✅ **包含完整的模型结构和 forward 函数**
- ✅ **可独立运行，无需 Python 类定义**
- ✅ **可在 C++ 中直接加载和推理**
- ✅ **已优化为部署格式**
- ✅ 包含归一化器（normalizer）等完整推理链路
- ❌ 不包含优化器状态，不用于训练

## 🎯 核心区别

| 特性 | model_XXXX.pt (训练检查点) | exported/policy.pt (TorchScript) |
|------|---------------------------|----------------------------------|
| **模型结构** | ❌ 仅参数，无结构 | ✅ 完整结构 + 参数 |
| **forward 函数** | ❌ 无 | ✅ 有 |
| **直接推理** | ❌ 需要重建模型结构 | ✅ 直接加载即用 |
| **C++ 推理** | ❌ 不支持 | ✅ 支持 |
| **MuJoCo 仿真** | ❌ 不支持 | ✅ 支持 |
| **Python 推理** | ⚠️ 需要模型定义 | ✅ 开箱即用 |
| **用途** | 训练/继续训练 | 部署/推理 |

## 🔧 为什么会有这个区别？

### Isaac Lab (RSL_RL) 的保存方式

Isaac Lab 使用 PyTorch 的标准 checkpoint 保存方式：

```python
# robot_lab/source/extensions/omni.isaac.lab_tasks/omni/isaac/lab_tasks/utils/wrappers/rsl_rl/rl_cfg.py
torch.save({
    'model_state_dict': self.agent.actor_critic.state_dict(),
    'optimizer_state_dict': self.agent.optimizer.state_dict(),
    'iter': self.current_learning_iteration,
    'infos': {...}
}, checkpoint_path)
```

**只保存了参数字典，不保存模型结构！**

### 为什么不直接保存模型？

1. **训练灵活性**：可以修改模型结构后仍然加载权重
2. **文件更小**：不包含代码，只有参数
3. **PyTorch 惯例**：标准的训练检查点格式

## 📝 导出流程

使用 `export_isaac_lab_model.py` 将训练检查点转换为 TorchScript：

```bash
python export_isaac_lab_model.py \
    robot_lab/logs/rsl_rl/unitree_go2_rough/2025-12-24_16-11-25/model_23599.pt \
    -o robot_lab/logs/rsl_rl/unitree_go2_rough/2025-12-24_16-11-25/exported/policy.pt
```

**导出过程：**

1. **加载检查点** → 获取 `model_state_dict`
2. **分析架构** → 从参数形状推断网络结构
   ```python
   actor.0.weight: [512, 45]  # 输入层: 45 → 512
   actor.2.weight: [256, 512] # 隐藏层: 512 → 256
   actor.4.weight: [128, 256] # 隐藏层: 256 → 128
   actor.6.weight: [12, 128]  # 输出层: 128 → 12
   ```
3. **重建模型** → 创建 `ActorNetwork` 类实例
4. **加载权重** → `model.load_state_dict(actor_state_dict)`
5. **转换为 TorchScript** → `torch.jit.trace(model, dummy_input)`
6. **保存** → `traced_model.save(output_path)`

## 🚀 使用示例

### ❌ 错误用法：直接使用训练检查点

```python
# 这样无法工作！
checkpoint = torch.load("model_23599.pt")
# checkpoint 只是一个字典，无法调用 forward！
output = checkpoint(input)  # ❌ 错误：dict 不可调用
```

### ✅ 正确用法 1：使用导出的 TorchScript

```python
# Python 推理
policy = torch.jit.load("exported/policy.pt")
policy.eval()

with torch.no_grad():
    actions = policy(observations)  # ✅ 正确
```

```cpp
// C++ 推理
torch::jit::script::Module policy;
policy = torch::jit::load("exported/policy.pt");
policy.eval();

auto actions = policy.forward({observations}).toTensor();  // ✅ 正确
```

### ✅ 正确用法 2：手动重建模型结构

如果你非要使用 `model_23599.pt`：

```python
# 1. 定义模型结构（必须与训练时完全一致）
class ActorNetwork(nn.Module):
    def __init__(self):
        super().__init__()
        self.actor = nn.Sequential(
            nn.Linear(45, 512), nn.ELU(),
            nn.Linear(512, 256), nn.ELU(),
            nn.Linear(256, 128), nn.ELU(),
            nn.Linear(128, 12)
        )
    
    def forward(self, x):
        return self.actor(x)

# 2. 创建模型实例
model = ActorNetwork()

# 3. 加载权重
checkpoint = torch.load("model_23599.pt")
state_dict = checkpoint['model_state_dict']
actor_state_dict = {k.replace('actor.', ''): v 
                    for k, v in state_dict.items() 
                    if k.startswith('actor.')}
model.actor.load_state_dict(actor_state_dict)

# 4. 推理
model.eval()
with torch.no_grad():
    actions = model(observations)  # ✅ 可以工作，但很麻烦
```

**但这种方法的问题：**
- 需要手动定义模型结构
- 容易出错（层数、维度必须精确匹配）
- 无法用于 C++ 推理
- 需要维护额外的模型定义代码

## 🏗️ 在 MuJoCo 仿真中的应用

### robot_lab 的 MuJoCo 仿真环境

在 `robot_lab` 项目中使用 MuJoCo 进行仿真时：

```python
# robot_lab/scripts/play.py 或相关推理脚本
import torch

# ✅ 正确：使用导出的 TorchScript 模型
policy_path = "logs/rsl_rl/unitree_go2_rough/2025-12-24_16-11-25/exported/policy.pt"
policy = torch.jit.load(policy_path, map_location='cpu')
policy.eval()

# 仿真循环
for step in range(1000):
    # 获取观察
    obs = get_observations_from_mujoco()
    
    # 推理
    with torch.no_grad():
        actions = policy(torch.tensor(obs).unsqueeze(0))
    
    # 应用动作
    apply_actions_to_mujoco(actions[0].numpy())
```

### ❌ 如果使用训练检查点会发生什么

```python
# ❌ 错误方式
policy_path = "logs/rsl_rl/unitree_go2_rough/2025-12-24_16-11-25/model_23599.pt"
policy = torch.load(policy_path)  # 加载的是字典，不是模型

# 尝试推理
actions = policy(observations)  # ❌ TypeError: 'dict' object is not callable

# 即使你尝试访问 state_dict
actions = policy['model_state_dict'](observations)  # ❌ 依然错误
```

## 📊 文件结构对比验证

### 训练检查点

```python
>>> checkpoint = torch.load("model_23599.pt")
>>> type(checkpoint)
<class 'dict'>
>>> list(checkpoint.keys())
['model_state_dict', 'optimizer_state_dict', 'iter', 'infos']
>>> type(checkpoint['model_state_dict'])
<class 'collections.OrderedDict'>
>>> hasattr(checkpoint, 'forward')
False
```

### 导出的 TorchScript

```python
>>> policy = torch.jit.load("exported/policy.pt")
>>> type(policy)
<class 'torch.jit._script.RecursiveScriptModule'>
>>> hasattr(policy, 'forward')
True
>>> policy.code
def forward(self, x: Tensor) -> Tensor:
    actor = self.actor
    normalizer = self.normalizer
    _0 = (actor).forward((normalizer).forward(x, ), )
    return _0
```

## 🎓 与 Isaac Gym 的对比

### Isaac Gym 的优势

Isaac Gym (rl_games) 可能会：
- 直接保存完整模型对象（包含 forward）
- 或自动导出为 TorchScript

所以 Isaac Gym 的模型可以直接用于推理。

### Isaac Lab 的设计哲学

Isaac Lab 采用了更标准的 PyTorch 训练流程：
- **训练时**：只保存参数（checkpoint）
- **部署时**：显式导出为 TorchScript

这种设计：
- ✅ 更灵活（训练和部署分离）
- ✅ 更标准（符合 PyTorch 最佳实践）
- ⚠️ 需要额外的导出步骤

## 💡 最佳实践

### 1. 训练完成后立即导出

```bash
# 训练完成后
python export_isaac_lab_model.py \
    logs/rsl_rl/unitree_go2_rough/YYYY-MM-DD_HH-MM-SS/model_XXXX.pt
```

### 2. 验证导出模型

```python
# test_exported_model.py
import torch

policy = torch.jit.load("exported/policy.pt")
dummy_obs = torch.randn(1, 45)  # 根据你的观察空间调整

with torch.no_grad():
    actions = policy(dummy_obs)
    
print(f"✓ Model loaded successfully!")
print(f"  Input shape: {dummy_obs.shape}")
print(f"  Output shape: {actions.shape}")
```

### 3. 在推理代码中使用

```python
# 始终使用 exported/policy.pt
POLICY_PATH = "logs/rsl_rl/unitree_go2_rough/2025-12-24_16-11-25/exported/policy.pt"
```

### 4. 版本控制

```bash
exported/
├── policy.pt          # TorchScript 模型（用于推理）
├── policy.onnx        # ONNX 格式（可选）
└── metadata.json      # 记录训练信息
```

## 🔗 相关文件

- **导出脚本**: `/home/yzy/MyProject/rl_sar/export_isaac_lab_model.py`
- **训练检查点**: `robot_lab/logs/rsl_rl/*/model_*.pt`
- **导出模型**: `robot_lab/logs/rsl_rl/*/exported/policy.pt`

## 📚 总结

| 问题 | 答案 |
|------|------|
| **为什么不能用 `model_23599.pt`？** | 它只包含参数字典，没有模型结构和 forward 函数 |
| **为什么必须用 `exported/policy.pt`？** | 它是完整的 TorchScript 模型，包含结构、forward 函数和参数 |
| **如何生成 `exported/policy.pt`？** | 使用 `export_isaac_lab_model.py` 脚本 |
| **Isaac Gym 为什么不需要？** | Isaac Gym 可能直接保存完整模型或自动导出 |
| **可以手动加载 checkpoint 吗？** | 可以，但需要手动重建模型结构，且无法用于 C++ |

---

**关键要点：**
- ✅ **MuJoCo 仿真**：使用 `exported/policy.pt`
- ✅ **C++ 推理**：使用 `exported/policy.pt`
- ✅ **Python 快速推理**：使用 `exported/policy.pt`
- ⚠️ **继续训练**：使用 `model_XXXX.pt`
- ⚠️ **分析训练过程**：使用 `model_XXXX.pt`

**记住：部署推理永远使用 `exported/policy.pt`！**
