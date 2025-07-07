# PPPoE 认证配置快速指南

## 认证方式选择

pppcpd 支持两种主要的 PPP 认证方式：CHAP 和 PAP。可以通过 `lcp_conf` 配置段来选择。

### CHAP 认证（推荐）
```yaml
lcp_conf:
  insert_magic_number: true
  mru: 1492
  auth_chap: true    # 启用 CHAP
  auth_pap: false    # 禁用 PAP
```

**特点：**
- 更安全，密码不以明文传输
- 使用挑战-响应机制
- 适用于大多数现代 PPPoE 客户端

### PAP 认证
```yaml
lcp_conf:
  insert_magic_number: true
  mru: 1492
  auth_chap: false   # 禁用 CHAP
  auth_pap: true     # 启用 PAP
```

**特点：**
- 较简单，兼容性好
- 用户名密码明文传输（安全性较低）
- 适用于传统或特殊客户端

### 无认证模式
```yaml
lcp_conf:
  insert_magic_number: true
  mru: 1492
  auth_chap: false   # 禁用 CHAP
  auth_pap: false    # 禁用 PAP
```

**特点：**
- 跳过认证直接分配 IP
- 适用于测试环境
- 使用 `aaa_conf.method: [NONE]`

## 配置步骤

1. **生成配置文件模板**
   ```bash
   ./pppcpd --genconf
   ```

2. **编辑认证方式**
   ```yaml
   # 选择 CHAP 或 PAP，不要同时启用
   lcp_conf:
     auth_chap: true    # CHAP 认证
     auth_pap: false    # PAP 认证
   ```

3. **配置 AAA 方式**
   ```yaml
   aaa_conf:
     method:
       - NONE        # 本地认证，使用 local_template
       # - RADIUS    # 或者使用 RADIUS 认证
   ```

4. **启动服务**
   ```bash
   ./pppcpd -p config.yaml
   ```

## 常见问题

**Q: CHAP 和 PAP 可以同时启用吗？**
A: 不建议。通常只启用一种认证方式。

**Q: 如何判断客户端支持哪种认证方式？**
A: 可以先尝试 CHAP，如果连接失败再尝试 PAP。

**Q: 无认证模式下还需要配置 AAA 吗？**
A: 需要设置 `method: [NONE]` 和 `local_template`。

**Q: 认证配置错误会有什么现象？**
A: 客户端会在 LCP 协商后认证失败，无法获得 IP 地址。
