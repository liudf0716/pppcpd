# PPPoE Control Plane Daemon (pppcpd) 配置文档

## 概述
pppcpd 是一个与 VPP PPPoE Plugin 协同工作的 PPPoE 控制平面守护进程。本文档详细说明了所有配置选项。

## 配置文件格式
配置文件使用 YAML 格式，默认配置文件名为 `config.yaml`。可以通过命令行参数 `-p` 或 `--path` 指定自定义配置文件路径。

## 配置选项详解

### 1. 基本配置

#### `tap_name`
- **类型**: string
- **默认值**: "tap0"
- **说明**: TAP 接口名称，用于与 VPP 通信

```yaml
tap_name: tap0
```

#### `log_level`
- **类型**: string
- **默认值**: "INFO"
- **说明**: 日志级别，控制日志输出详细程度

支持的日志级别：
- **TRACE**: 跟踪级别，输出最详细的信息
- **DEBUG**: 调试级别，用于调试和故障排除
- **INFO**: 信息级别，输出一般信息（推荐用于生产环境）
- **WARNING**: 警告级别，只输出警告和错误信息
- **ERROR**: 错误级别，只输出错误信息
- **CRITICAL**: 严重级别，只输出严重错误信息

```yaml
log_level: INFO
```

#### `log_file`
- **类型**: string
- **默认值**: ""（空字符串，输出到控制台）
- **说明**: 日志文件路径。如果设置，日志将写入指定文件；如果为空或未设置，日志将输出到控制台

```yaml
log_file: /var/log/pppcpd.log  # 输出到文件
# log_file: ""                 # 或留空输出到控制台
```

**注意事项**：
- 确保 pppcpd 进程有权限写入指定的日志文件路径
- 日志文件以追加模式（append）打开，不会覆盖已有内容
- 建议配合 logrotate 等工具管理日志文件大小

### 2. 接口配置 (`interfaces`)

#### 接口配置结构
```yaml
interfaces:
  - device: G0                    # 接口设备名称
    admin_state: true             # 管理状态（启用/禁用）
    mtu: 1500                     # 最大传输单元（可选）
    units:                        # 子接口配置
      200:                        # VLAN ID
        admin_state: true         # 子接口管理状态
        vlan: 200                 # VLAN 标签
        address: 10.0.0.1/24      # IP 地址（可选）
        vrf: RED                  # VRF 名称（可选）
```

#### 配置说明
- **device**: 物理接口名称，支持简化命名（如 G0, G1）
- **admin_state**: 接口是否启用
- **mtu**: 接口 MTU 值（可选）
- **units**: 子接口（VLAN）配置映射
  - **vlan**: VLAN 标签 ID
  - **address**: 接口 IP 地址（可选）
  - **vrf**: 绑定的 VRF 名称（可选）

### 3. PPPoE 配置

#### 默认 PPPoE 配置 (`default_pppoe_conf`)
```yaml
default_pppoe_conf:
  ac_name: "vBNG AC PPPoE"        # 接入集中器名称
  service_name:                   # 支持的服务名称列表
    - inet
    - pppoe
  insert_cookie: true             # 是否插入 Cookie
  ignore_service_name: true       # 是否忽略服务名称检查
```

#### VLAN 特定 PPPoE 配置 (`pppoe_confs`)
```yaml
pppoe_confs:
  100:                           # VLAN ID
    ac_name: "Custom AC"
    service_name:
      - custom_service
    insert_cookie: false
    ignore_service_name: false
```

#### PPPoE 模板配置 (`pppoe_templates`)
```yaml
pppoe_templates:
  template1:                     # 模板名称
    framed_pool: pppoe_pool1     # IP 地址池名称
    dns1: 8.8.8.8               # 主 DNS 服务器
    dns2: 1.1.1.1               # 备用 DNS 服务器
    unnumbered: G1.150          # Unnumbered 接口
    vrf: RED                    # VRF 名称（可选）
```

### 4. AAA 配置 (`aaa_conf`)

#### 认证方法 (`method`)
```yaml
aaa_conf:
  method:
    - NONE                      # 无认证
    - RADIUS                    # RADIUS 认证
```

支持的认证方法：
- **NONE**: 无认证模式
- **LOCAL**: 本地认证（预留）
- **RADIUS**: RADIUS 认证

#### IP 地址池 (`pools`)
```yaml
pools:
  pppoe_pool1:                  # 地址池名称
    start_ip: 100.64.0.10       # 起始 IP 地址
    stop_ip: 100.64.255.255     # 结束 IP 地址
  vrf_pool1:
    start_ip: 100.66.0.10
    stop_ip: 100.66.0.255
```

#### RADIUS 服务器配置
```yaml
auth_servers:                   # 认证服务器
  main_auth_1:                  # 服务器名称
    address: 127.0.0.1          # 服务器地址
    port: 1812                  # 服务器端口
    secret: testing123          # 共享密钥

acct_servers:                   # 计费服务器
  main_acct_1:
    address: 127.0.0.1
    port: 1813
    secret: testing123
```

#### RADIUS 字典文件 (`dictionaries`)
```yaml
dictionaries:
  - /usr/share/freeradius/dictionary.rfc2865
  - /usr/share/freeradius/dictionary.rfc2866
  - /usr/share/freeradius/dictionary.rfc2869
  - /usr/share/freeradius/dictionary.ericsson.ab
```

#### 本地模板 (`local_template`)
```yaml
local_template: template1       # 用于无认证/本地认证的默认模板
```

### 5. LCP 配置 (`lcp_conf`) - **新增功能**

```yaml
lcp_conf:
  insert_magic_number: true     # 是否插入魔术数字
  mru: 1492                     # 最大接收单元
  auth_chap: true               # 启用 CHAP 认证
  auth_pap: false               # 启用 PAP 认证
```

#### 认证方式配置说明
- **auth_chap**: 启用 CHAP (Challenge Handshake Authentication Protocol) 认证
  - 更安全的认证方式
  - 使用挑战-响应机制
  - 密码不以明文传输
  
- **auth_pap**: 启用 PAP (Password Authentication Protocol) 认证
  - 较简单的认证方式
  - 用户名和密码以明文传输
  - 兼容性更好但安全性较低

**注意**: `auth_chap` 和 `auth_pap` 通常只能选择其中一个启用。如果两者都设为 false，则跳过认证直接进入 IPCP 协商。

#### 配置示例

**使用 CHAP 认证**（推荐）：
```yaml
lcp_conf:
  insert_magic_number: true
  mru: 1492
  auth_chap: true
  auth_pap: false
```

**使用 PAP 认证**：
```yaml
lcp_conf:
  insert_magic_number: true
  mru: 1492
  auth_chap: false
  auth_pap: true
```

**无认证模式**：
```yaml
lcp_conf:
  insert_magic_number: true
  mru: 1492
  auth_chap: false
  auth_pap: false
```

### 6. 路由配置

#### 全局路由表 (`global_rib`)
```yaml
global_rib:
  entries:
    - destination: 0.0.0.0/0     # 目标网络
      nexthop: 10.0.0.1          # 下一跳地址
      description: default gateway # 描述（可选）
```

#### VRF 配置 (`vrfs`)
```yaml
vrfs:
  - name: RED                    # VRF 名称
    table_id: 10                 # 路由表 ID
    rib:                         # VRF 路由表
      entries:
        - destination: 0.0.0.0/0
          nexthop: 10.10.0.1
          description: default gateway
```

## 命令行选项

### 生成示例配置
```bash
./pppcpd --genconf
# 或
./pppcpd -g
```

### 指定配置文件
```bash
./pppcpd --path /path/to/config.yaml
# 或
./pppcpd -p /path/to/config.yaml
```

### 查看帮助
```bash
./pppcpd --help
# 或
./pppcpd -h
```

## 配置文件验证

启动 pppcpd 时，会自动验证配置文件的语法和内容。如果配置文件有错误，程序会输出详细的错误信息并退出。

## 常见配置场景

### 1. 基本 PPPoE 配置（CHAP 认证）
适用于标准的 PPPoE 部署，使用 CHAP 认证和 RADIUS 后端。

### 2. 多 VRF 环境
支持多个 VRF，每个 VRF 可以有独立的地址池和路由表。

### 3. 本地认证
使用 `NONE` 认证方法，适用于测试环境或不需要认证的场景。

### 4. PAP 认证兼容
对于需要 PAP 认证的传统设备，可以配置 `auth_pap: true`。

## 注意事项

1. **接口命名**: 支持 VPP 风格的接口命名（如 GigabitEthernet0/8/0）和简化命名（如 G0）
2. **VLAN 配置**: 确保 VLAN ID 在网络中唯一且正确配置
3. **IP 地址池**: 避免地址池重叠，确保有足够的地址供分配
4. **RADIUS 配置**: 确保 RADIUS 服务器可达且密钥正确
5. **认证方式**: 根据客户端支持情况选择合适的认证方式（CHAP 或 PAP）

## 故障排除

1. **配置语法错误**: 检查 YAML 语法，特别注意缩进
2. **IP 地址冲突**: 检查地址池配置和接口地址配置
3. **RADIUS 连接失败**: 检查网络连通性和认证信息
4. **VPP 连接问题**: 确保 VPP 正在运行且 API 可用
5. **认证失败**: 检查 LCP 认证配置和 AAA 配置的一致性
