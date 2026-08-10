// 云函数：sendWallpaper —— 表盘背景图下发给手表（v2：MQTT 传图）
// 流程：小程序上传图片到云存储 → fileID → 本函数云端下载图片（云函数有完整 TLS）
//       → base64 编码 → ThingsCloud 下发 data/wallpaper/set {"b64":"..."}
//       → 手表 MQTT 收到 base64 → 解码存 LittleFS（绕开 ESP32 直连 HTTPS 的证书问题）
// 凭据：AccessToken / Project-Key 来自 ThingsCloud 设备证书（免费版应用端 API），只存在云端。

const cloud = require('wx-server-sdk')
cloud.init({ env: cloud.DYNAMIC_CURRENT_ENV })
const https = require('https')

// ===== ThingsCloud 配置（设备证书，免费版应用端 API）=====
const API_HOST     = 'gz-3-api.iot-api.com'
const ACCESS_TOKEN = 'YOUR_ACCESS_TOKEN'
const PROJECT_KEY  = 'YOUR_PROJECT_KEY'

// base64 长度上限：对齐手表端 MQTT 接收缓冲（24KB，留余量）
const MAX_B64 = 22000

// 云存储 fileID → 图片 Buffer
async function downloadFromCloud(fileID) {
  const res = await cloud.downloadFile({ fileID })
  return res.fileContent   // Buffer
}

// 手动测试用：https URL → 图片 Buffer
function downloadFromUrl(url) {
  return new Promise((resolve, reject) => {
    https.get(url, (res) => {
      if (res.statusCode !== 200) { res.resume(); return reject(new Error('HTTP ' + res.statusCode)) }
      const chunks = []
      res.on('data', (c) => chunks.push(c))
      res.on('end', () => resolve(Buffer.concat(chunks)))
    }).on('error', reject)
  })
}

// 调用 ThingsCloud 应用端 API 下发自定义数据流
function sendToThings(b64) {
  return new Promise((resolve, reject) => {
    const body = JSON.stringify({ type: 'json', msg: { b64 } })
    const req = https.request({
      hostname: API_HOST,
      path: '/app/device/v1/' + ACCESS_TOKEN + '/data/wallpaper/set?push_mode=mqtt',
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
        'Project-Key': PROJECT_KEY,
        'Content-Length': Buffer.byteLength(body)
      }
    }, (res) => {
      let data = ''
      res.on('data', (c) => { data += c })
      res.on('end', () => {
        try { resolve(JSON.parse(data)) } catch (e) { resolve({ raw: data }) }
      })
    })
    req.on('error', reject)
    req.setTimeout(15000, () => req.destroy(new Error('request timeout')))
    req.write(body)
    req.end()
  })
}

exports.main = async (event, context) => {
  try {
    let buffer
    if (event && event.fileID) {
      buffer = await downloadFromCloud(event.fileID)
    } else if (event && event.url) {
      buffer = await downloadFromUrl(event.url)
    } else {
      return { ok: false, msg: '缺少 fileID 或 url' }
    }
    if (!buffer || buffer.length === 0) return { ok: false, msg: '下载图片失败' }

    const b64 = buffer.toString('base64')
    if (b64.length > MAX_B64) {
      return { ok: false, msg: '图片过大（base64 ' + b64.length + ' 字节，上限 ' + MAX_B64 + '），请换更小的图' }
    }

    const resp = await sendToThings(b64)
    return { ok: resp && resp.result === 1, resp, imgBytes: buffer.length, b64len: b64.length }
  } catch (err) {
    console.error('sendWallpaper error:', err)
    return { ok: false, msg: err.message }
  }
}
