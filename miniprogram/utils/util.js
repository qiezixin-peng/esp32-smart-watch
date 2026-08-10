// utils/util.js - 通用工具函数
// 时间戳格式化：yyyy-MM-dd HH:mm
function formatTime(ts) {
  const d = new Date(ts)
  const pad = (n) => (n < 10 ? '0' + n : '' + n)
  return `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())} ${pad(d.getHours())}:${pad(d.getMinutes())}`
}

// 时间戳格式化（仅时分）
function formatHM(ts) {
  const d = new Date(ts)
  const pad = (n) => (n < 10 ? '0' + n : '' + n)
  return `${pad(d.getHours())}:${pad(d.getMinutes())}`
}

module.exports = { formatTime, formatHM }