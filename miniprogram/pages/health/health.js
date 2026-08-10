// pages/health/health.js - 健康报表：属性历史折线图
const { formatHM } = require('../../utils/util')

// 可选的属性列表
const METRICS = [
  { key: 'temp',  name: '温度',  unit: '℃',  color: '#ff6b6b' },
  { key: 'hum',   name: '湿度',  unit: '%',  color: '#3b82f6' },
  { key: 'press', name: '气压',  unit: 'hPa', color: '#8b5cf6' },
  { key: 'bpm',   name: '心率',  unit: '次/分', color: '#ec4899' },
  { key: 'spo2',  name: '血氧',  unit: '%',  color: '#10b981' },
  { key: 'uv',    name: '紫外线', unit: '',  color: '#f59e0b' }
]

// 颜色 hex → rgba（趋势图渐变面积填充用）
function hexToRgba(hex, alpha) {
  const n = parseInt(hex.slice(1), 16)
  const r = (n >> 16) & 255, g = (n >> 8) & 255, b = n & 255
  return `rgba(${r},${g},${b},${alpha})`
}

Page({
  data: {
    metrics: METRICS,
    activeIndex: 0,
    activeMetric: METRICS[0],
    records: [],          // 原始记录
    chartData: [],        // 图表数据点 {t, v}
    loading: true,
    empty: false,
    countText: ''
  },

  onLoad() {
    this.loadData()
  },

  onReady() {
    // canvas 节点准备后绘制（onReady 时查询节点）
    this.drawChart()
  },

  async loadData() {
    this.setData({ loading: true, empty: false })
    try {
      // 打开页面时先采集一次最新数据（替代后台定时轮询，省配额）
      try { await wx.cloud.callFunction({ name: 'collectSensorData' }) } catch (e) { /* ignore */ }
      const db = wx.cloud.database()
      // 最近 120 条（约 2 小时，1 分钟粒度）
      const res = await db.collection('health_records')
        .orderBy('ts', 'desc')
        .limit(120)
        .get()
      const records = (res.data || []).reverse() // 时间正序
      this.setData({
        records,
        loading: false,
        empty: records.length === 0,
        countText: records.length ? `共 ${records.length} 条数据` : ''
      })
      this.buildChartData()
      this.drawChart()
    } catch (e) {
      console.error('读取健康数据失败', e)
      this.setData({ loading: false, empty: true })
    }
  },

  // 根据当前选中属性构建图表数据
  buildChartData() {
    const m = this.data.metrics[this.data.activeIndex]
    const chartData = this.data.records.map(r => ({
      t: r.ts,
      v: r[m.key] != null ? Number(r[m.key]) : null
    }))
    this.setData({ chartData })
  },

  // 切换属性
  onTapMetric(e) {
    const idx = Number(e.currentTarget.dataset.idx)
    if (idx === this.data.activeIndex) return
    this.setData({ activeIndex: idx, activeMetric: this.data.metrics[idx] })
    this.buildChartData()
    this.drawChart()
  },

  // 用 canvas 2d 绘制折线图
  async drawChart() {
    const m = this.data.activeMetric
    const chartData = this.data.chartData.filter(p => p.v != null)
    if (chartData.length === 0) return

    const query = wx.createSelectorQuery()
    const node = await new Promise((resolve) => {
      query.select('#chart').fields({ node: true, size: true }).exec((res) => {
        if (res && res[0]) resolve(res[0]); else resolve(null)
      })
    })
    if (!node) return

    const canvas = node.node
    const ctx = canvas.getContext('2d')
    const dpr = wx.getSystemInfoSync().pixelRatio
    canvas.width = node.width * dpr
    canvas.height = node.height * dpr
    ctx.scale(dpr, dpr)

    const W = node.width
    const H = node.height
    const padL = 46, padR = 16, padT = 20, padB = 34

    ctx.clearRect(0, 0, W, H)
    // 背景透明：透出卡片浅灰底，与白色主题统一

    // 计算值域
    const values = chartData.map(p => p.v)
    let vMin = Math.min.apply(null, values)
    let vMax = Math.max.apply(null, values)
    if (vMax - vMin < 1) { vMax += 0.5; vMin -= 0.5 }
    const span = vMax - vMin
    vMin -= span * 0.1
    vMax += span * 0.1

    const innerW = W - padL - padR
    const innerH = H - padT - padB
    const x = (i) => padL + (chartData.length === 1 ? innerW / 2 : (innerW * i) / (chartData.length - 1))
    const y = (v) => padT + innerH - ((v - vMin) / (vMax - vMin)) * innerH

    // 网格线 + Y 轴刻度（4 格）
    ctx.strokeStyle = '#e6ebf3'
    ctx.lineWidth = 1
    ctx.fillStyle = '#9aa4b5'
    ctx.font = '10px sans-serif'
    for (let g = 0; g <= 4; g++) {
      const gy = padT + (innerH * g) / 4
      ctx.beginPath()
      ctx.moveTo(padL, gy)
      ctx.lineTo(W - padR, gy)
      ctx.stroke()
      const val = vMax - ((vMax - vMin) * g) / 4
      ctx.fillText(val.toFixed(1), 4, gy + 3)
    }

    // 渐变面积填充（折线下方渐隐，现代健康 App 风格）
    const grad = ctx.createLinearGradient(0, padT, 0, H - padB)
    grad.addColorStop(0, hexToRgba(m.color, 0.28))
    grad.addColorStop(1, hexToRgba(m.color, 0.02))
    ctx.beginPath()
    chartData.forEach((p, i) => {
      const px = x(i), py = y(p.v)
      if (i === 0) ctx.moveTo(px, py); else ctx.lineTo(px, py)
    })
    ctx.lineTo(x(chartData.length - 1), padT + innerH)
    ctx.lineTo(x(0), padT + innerH)
    ctx.closePath()
    ctx.fillStyle = grad
    ctx.fill()

    // 折线（圆角连接，柔和）
    ctx.strokeStyle = m.color
    ctx.lineWidth = 2
    ctx.lineJoin = 'round'
    ctx.lineCap = 'round'
    ctx.beginPath()
    chartData.forEach((p, i) => {
      const px = x(i), py = y(p.v)
      if (i === 0) ctx.moveTo(px, py); else ctx.lineTo(px, py)
    })
    ctx.stroke()

    // 数据点（白边圆点，点缀不抢眼）
    const step = Math.max(1, Math.floor(chartData.length / 60))
    chartData.forEach((p, i) => {
      if (i % step !== 0 && i !== chartData.length - 1) return
      ctx.beginPath()
      ctx.arc(x(i), y(p.v), 3, 0, Math.PI * 2)
      ctx.fillStyle = '#ffffff'
      ctx.fill()
      ctx.lineWidth = 1.5
      ctx.strokeStyle = m.color
      ctx.stroke()
    })

    // X 轴时间刻度（首/中/尾）
    ctx.fillStyle = '#9aa4b5'
    ctx.font = '10px sans-serif'
    ctx.textAlign = 'center'
    const first = chartData[0].t
    const mid = chartData[Math.floor(chartData.length / 2)].t
    const last = chartData[chartData.length - 1].t
    ctx.fillText(formatHM(first), padL, H - 10)
    ctx.fillText(formatHM(mid), padL + innerW / 2, H - 10)
    ctx.fillText(formatHM(last), W - padR, H - 10)
    ctx.textAlign = 'left'

    // 标题信息
    ctx.fillStyle = '#7a8499'
    ctx.font = '12px sans-serif'
    ctx.fillText(`${m.name} (${m.unit})`, padL, 14)
  }
})