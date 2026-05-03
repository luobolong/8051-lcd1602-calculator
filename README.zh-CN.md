# 8051 LCD1602 计算器

[English README](README.md)

STC89C52RC（普中 51 A2 开发板）定点计算器。LCD1602 显示，4x4 矩阵键盘 +
4 个独立按键。单文件 C 实现，零标准库依赖，Flash 占用约 6 KB。

## 硬件

### LCD1602

| 信号 | 引脚 |
| --- | --- |
| DB0-DB7 | P0 |
| RS / RW / EN | P2.6 / P2.5 / P2.7 |

### 矩阵键盘 (P1)

```text
S1  S2  S3  S4      7  8  9  +
S5  S6  S7  S8      4  5  6  -
S9  S10 S11 S12     1  2  3  *
S13 S14 S15 S16    +/- 0  .  /
```

### 独立按键

| 按键 | 功能 | 引脚 |
| --- | --- | --- |
| R1 | 退格 | P3.1 |
| R2 | AC | P3.0 |
| R3 | 百分号 | P3.2 |
| R4 | 等于 | P3.3 |

## 编译

### 工具链 (Arch Linux)

```sh
sudo pacman -S sdcc make
pip install stcgal
sudo usermod -aG uucp $USER   # 串口访问权限，重新登录生效
```

### 编译和烧录

```sh
make            # -> build/main.hex
make flash      # 默认: /dev/ttyUSB0, 115200 baud, stc89 协议
```

### SDCC 编译参数

```sh
sdcc -mmcs51 --model-small --iram-size 256 --idata-loc 0x80
```

| 参数 | 作用 |
| --- | --- |
| `-mmcs51` | 目标架构 MCS-51 |
| `--model-small` | 所有变量默认分配到内部 RAM，使用直接寻址 |
| `--iram-size 256` | 启用 8052 风格 256 字节内部 RAM（上半区通过间接寻址访问） |
| `--idata-loc 0x80` | 将 `__idata` 对象放在 0x80，把低区 RAM 留给直接寻址的全局变量 |

## 架构

单文件设计（约 900 行）。无头文件、无库依赖、无链接复杂度。代码按调用层次
组织，每层只向下调用：

```text
主循环
  ├─ scan_key_action              硬件输入
  │    ├─ scan_independent_action   R1-R4 消抖
  │    └─ scan_matrix_raw           4x4 列/行扫描
  ├─ handle_action                计算器状态机
  │    ├─ input_digit / input_dot / input_sign
  │    ├─ input_operator / input_equal
  │    ├─ input_percent / input_backspace
  │    └─ compute
  │         ├─ fixed_add
  │         ├─ fixed_mul            四项分解乘法
  │         └─ fixed_div
  └─ render                       LCD 输出
       ├─ build_live_expr           格式化 "左操作数 运算符 右操作数"
       ├─ format_scaled             定点数 -> ASCII
       └─ lcd_line_tail / lcd_line_right
```

- 编辑状态（`g_edit_abs`、`g_edit_neg`、`g_dot`、`g_frac_count`）与已提交
  状态（`g_left`、`g_op`）完全分离。退格在输入层面操作，移除最后输入的字符，
  而非反转数值运算。
- 硬件扫描和计算器逻辑通过一个 `action` 字节解耦。改接线只需改扫描层。
- 所有算术运算在提交前检查溢出，不会静默回绕。

## 定点数表示

所有数值以 `i32` 存储，放大 100 倍（`SCALE = 100L`）：

| 显示值 | 内部值 | 显示值 | 内部值 |
| --- | --- | --- | --- |
| `1` | `100` | `-5.5` | `-550` |
| `1.23` | `123` | `200000` | `20000000` |

两位小数。截断导致的最大相对误差为 1/SCALE = 1%（例如 1 / 3 = 0.33 而非
0.333...）。

### CALC_MAX = 200000.00 的由来

约束来自 `fixed_div`，它计算：

```c
result = (ua * SCALE) / ub;
```

中间值 `ua * SCALE` 必须在 `u32` 范围内：

```text
20000000 * 100 = 2 * 10^9 < 2^32 - 1 = 4.29 * 10^9   OK
```

更大的 CALC_MAX 会让除法安全但乘法子项溢出。20000000 是保证四则运算全部在
`u32` 内安全的最大整数值。

## 算术实现

### 乘法

朴素定点乘法 `(a * b) / SCALE` 在操作数超过约 655 时就会溢出 `u32`
（65536^2 > 2^32）。实现将每个操作数拆分为整数部分和小数部分：

```text
a = ai * SCALE + af
b = bi * SCALE + bf

a * b / SCALE = ai * bi * SCALE     项 1
              + ai * bf              项 2
              + bi * af              项 3
              + af * bf / SCALE      项 4
```

每项通过 `add_abs_checked()` 独立检查溢出后再累加。项 4 不会溢出，因为
af, bf < 100，af * bf < 10000。

最坏情况：200000.00 * 200000.00。仅项 1 = 200000 * 200000 * 100 = 4 * 10^12，
溢出 `u32`。守卫条件 `ai > CALC_MAX / (bi * SCALE)` 在乘法执行前拦截。

### 除法

```c
result = (abs(a) * SCALE) / abs(b);
```

整数截断导致最大绝对误差为 1 个单位（显示 0.01）。除以零进入错误状态 1
（`Div by zero`），结果超出 CALC_MAX 进入错误状态 2（`Overflow`）。

### 加法

两个操作数共享相同缩放比例，直接相加。加法前检查溢出：`a > CALC_MAX - b`
（正数）或 `a < CALC_MIN - b`（负数）。减法复用 `fixed_add`，取反第二操作数。

## 输入状态机

| 变量 | 类型 | 作用 |
| --- | --- | --- |
| `g_left` | `i32` | 已提交的左操作数 / 结果 |
| `g_edit_abs` | `u32` | 正在输入数字的绝对值 |
| `g_edit_neg` | `__bit` | 正在输入数字的符号 |
| `g_dot` | `__bit` | 已输入小数点 |
| `g_frac_count` | `u8` | 已输入的小数位数 (0-2) |
| `g_has_digit` | `__bit` | 已输入至少一个数字 |
| `g_op` | `u8` | 待执行运算符（`+`/`-`/`*`/`/` 或 0） |
| `g_result_shown` | `__bit` | 当前显示的是计算结果 |
| `g_error` | `u8` | 错误状态（0=无, 1=除零, 2=溢出） |

状态转换：

- **数字**：小数点前 `edit_abs = edit_abs * 10 + d * SCALE`；
  小数点后 `edit_abs += d * frac_place[frac_count++]`。
- **小数点**：设置 `g_dot`，后续数字进入小数位。
- **正负号**：翻转 `g_edit_neg`。若当前显示结果，则取反 `g_left`。
- **运算符**：将编辑值提交到 `g_left`（或链式计算：执行 `g_left op edit`，
  结果存入 `g_left`），清空编辑状态准备下一个操作数。
- **等号**：计算 `g_left op edit`，存储结果，清除运算符。
- **退格**：按逆序移除最后输入的元素：小数位 -> 小数点 -> 整数位 -> 符号。
  若有待执行运算符但无编辑输入，则移除运算符。
- **百分号**：当前值除以 100（小数点左移两位）。
- **AC**：重置所有状态。

显示结果后，输入数字开始新计算；输入运算符则从结果继续链式运算。

## 内存预算

### 内部 RAM 映射

SDCC `main.mem` 输出（标注）：

```text
      0 1 2 3 4 5 6 7 8 9 A B C D E F
0x00:|0|0|0|0|0|0|0|0|Q|Q|Q|Q|Q|Q|Q| |  寄存器组 0 + 覆盖区
0x10:| | | | | | | | | | | | | | | | |  空闲（17 字节）
0x20:|B|a|a|a|a|a|a|a|a|a|a|a|a|a|a|a|  位寻址区 + 直接寻址数据
 ... |a|a|a|a|a|a|a|a|a|a|a|a|a|a|a| |
0x80:|I|I|I|I|I|I|I|I|I|I|I|I|I|I|I|I|  idata（仅间接寻址）
 ... |I|I|I|I|I|I|S|S|S|S|S|S|S|S|S|S|
0xF0:|S|S|S|S|S|S|S|S|S|S|S|S|S|S|S|S|  栈（向上增长）
```

### 分配明细

| 区域 | 地址 | 字节 | 内容 |
| --- | --- | --- | --- |
| 寄存器组 0 | 0x00-0x07 | 8 | R0-R7 |
| 覆盖区 (Q) | 0x08-0x0E | 7 | 不重叠调用路径的共享局部变量 |
| 位寻址区 (B) | 0x20 | 1 | 4 个 `__bit` 标志 |
| 直接寻址数据 (a) | 0x21-0x7E | 85 | `g_left`、`g_edit_abs`、`g_op`、`g_rev[8]` 等 |
| IDATA (I) | 0x80-0xD5 | 86 | `g_expr[24]`、`g_num[14]`、`fixed_mul` 局部变量 |
| 栈 (S) | 0xD6-0xFF | 42 | 调用栈（返回地址 + 临时变量） |
| Flash | 0x0000-0x1770 | 6001 | 代码 + `__code` 常量 |

### 栈深度

最深调用链：

```text
main -> handle_action -> input_operator -> compute -> fixed_mul
```

`fixed_mul` 的 10 个局部变量全部声明为 `__idata`，占用预分配的 IDATA 槽位
而非栈帧。42 字节栈预算覆盖返回地址（2 字节 * 约 5 层 = 10 字节）加上
非 `__idata` 局部变量和编译器临时变量。

### 优化手段

| 手段 | 机制 | 效果 |
| --- | --- | --- |
| `__code` 常量 | `g_key_map`、`g_frac_place` 放入 Flash | -20 B RAM |
| `__bit` 标志 | 4 个布尔值放入位寻址区 | -3 B RAM，单周期 `SETB`/`CLR`/`JB` |
| `__idata` 缓冲区 | `g_expr[24]`、`g_num[14]` 放入上半区 | -38 B 直接寻址 RAM |
| `__idata` 局部变量 | `fixed_mul` 临时变量放入上半区 | -40 B 栈 |
| SDCC 覆盖 | 不重叠函数共享 7 字节 | 相比独立分配节省约 23 B |
| 无 printf/float | 手写格式化 + 定点数 | -3~6 KB Flash |
| O(n) 字符串构建 | `append_text`/`append_u32` 跟踪长度 | 避免每字符 O(n^2) 重扫 |

## 时序

| 操作 | 耗时 | 说明 |
| --- | --- | --- |
| LCD EN 脉冲 | 2 ms | 高电平 1 ms + 低电平 1 ms |
| 完整 LCD 刷新 | ~64 ms | 32 字符 * 2 ms |
| 矩阵键消抖 | 12 ms | 初次检测后二次读取 |
| 矩阵键释放等待 | 10 ms/次 | 最多 200 次轮询（2 秒超时） |
| 独立键消抖 | 12 ms | 与矩阵键相同 |
| 空闲扫描周期 | ~2 ms | 矩阵探测 + 独立键检查 |

活跃输入时主循环约 15 Hz（受 LCD 刷新限制），空闲轮询约 500 Hz。

## 限制

| 属性 | 值 |
| --- | --- |
| 范围 | -200000.00 到 200000.00 |
| 小数位数 | 2（固定） |
| 最大相对误差 | 1%（除法整数截断） |
| 错误状态 | `Div by zero`（除以零）、`Overflow`（超出范围） |

结果中的尾零会被去除（`1.50` -> `1.5`），但输入过程中保留已输入的小数位。

## 修改指南

| 修改内容 | 位置 |
| --- | --- |
| LCD 引脚 | `LCD_RS`、`LCD_RW`、`LCD_EN` 的 `__sbit` 定义 |
| R1-R4 引脚 | `KEY_R1`-`KEY_R4` 的 `__sbit` 定义 |
| 按键布局 | `g_key_map[16]` 数组 |
| 小数精度 | `SCALE`、`SCALE_DIGITS`、`g_frac_place[]` — 改后需验证 RAM/Flash |
| 串口参数 | Makefile 中的 `PORT`、`BAUD`、`PROTOCOL` |
