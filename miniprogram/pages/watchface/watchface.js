// pages/watchface/watchface.js - 表盘设置：上传图片作为手表数字表盘背景（阶段 3）
// 流程：选图 → 压缩为 240x240 JPEG（居中裁剪）→ 云存储上传 → 云函数 sendWallpaper
//       把 fileID 转 HTTPS URL 下发到手表（data/wallpaper/set）→ 提示结果
const { formatTime } = require('../../utils/util')

Page({
  data: {
    preview: '',     // 本地预览图（压缩后）
    ready: false,    // 是否已选好图
    sending: false,  // 是否正在上传/下发
    status: '',      // 状态提示文本
    statusType: '',  // '' | 'ok' | 'err'
    lastAt: ''       // 上次下发时间
  },

  // 选择图片
  onChooseImage() {
    wx.chooseMedia({
      count: 1,
      mediaType: ['image'],
      sourceType: ['album', 'camera'],
      sizeType: ['compressed'],
      success: (res) => {
        const src = res.tempFiles[0].tempFilePath
        this.setData({ preview: src, ready: false, status: '', statusType: '' })
        this.compress(src)
      },
      fail: () => {} // 用户取消，不处理
    })
  },

  // 压缩：居中裁剪为正方形并缩放到 240x240 JPEG（手表屏幕分辨率）
  compress(src) {
    wx.getImageInfo({
      src,
      success: (info) => {
        const side = Math.min(info.width, info.height)
        const sx = Math.floor((info.width - side) / 2)
        const sy = Math.floor((info.height - side) / 2)
        const ctx = wx.createCanvasContext('wallpaperCanvas', this)
        ctx.drawImage(src, sx, sy, side, side, 0, 0, 240, 240)
        ctx.draw(false, () => {
          wx.canvasToTempFilePath({
            canvasId: 'wallpaperCanvas',
            fileType: 'jpg',
            quality: 0.8,
            destWidth: 240,
            destHeight: 240,
            success: (r) => this.setData({ preview: r.tempFilePath, ready: true }),
            fail: () => this.setData({ ready: true }) // 压缩失败则用原图
          }, this)
        })
      },
      fail: () => this.setData({ ready: true }) // 读图信息失败则用原图
    })
  },

  // 上传云存储 + 云函数下发
  async onUpload() {
    const { preview, ready, sending } = this.data
    if (!preview || !ready || sending) return

    this.setData({ sending: true, status: '上传中...', statusType: '' })
    try {
      const cloudPath = `wallpaper/${Date.now()}-${Math.floor(Math.random() * 1000)}.jpg`
      const up = await wx.cloud.uploadFile({ cloudPath, filePath: preview })
      this.setData({ status: '上传成功，正在下发到手表...' })

      const call = await wx.cloud.callFunction({
        name: 'sendWallpaper',
        data: { fileID: up.fileID }
      })
      const r = call.result || {}
      if (r.ok) {
        this.setData({
          status: '已下发到手表，请保持手表开机联网（2 小时内生效）',
          statusType: 'ok',
          lastAt: formatTime(Date.now())
        })
      } else {
        this.setData({
          status: '下发失败：' + (r.msg || JSON.stringify(r.resp || r)),
          statusType: 'err'
        })
      }
    } catch (e) {
      console.error('表盘图片上传失败', e)
      this.setData({
        status: '上传失败：' + (e.errMsg || e.message || e),
        statusType: 'err'
      })
    } finally {
      this.setData({ sending: false })
    }
  }
})
