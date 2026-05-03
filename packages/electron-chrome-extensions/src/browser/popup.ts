import { EventEmitter } from 'node:events'
import { BrowserWindow, Session } from 'electron'
import { getAllWindows } from './api/common'
import debug from 'debug'

const d = debug('electron-chrome-extensions:popup')

export interface PopupAnchorRect {
  x: number
  y: number
  width: number
  height: number
}

interface PopupViewOptions {
  extensionId: string
  session: Session
  parent: Electron.BaseWindow
  url: string
  anchorRect: PopupAnchorRect
  alignment?: string
}

const supportsPreferredSize = () => {
  const major = parseInt(process.versions.electron.split('.').shift() || '', 10)
  return major >= 12
}

// Matches the Pane sidebar background (#0A0A0B) for a seamless edge
const POPUP_CSS = [
  'html { margin: 0 !important; padding: 0 !important; }',
  'html::after {',
  '  content: "" !important;',
  '  position: fixed !important;',
  '  inset: 0 !important;',
  '  border: 1px solid #0A0A0B !important;',
  '  border-radius: 16px !important;',
  '  pointer-events: none !important;',
  '  z-index: 2147483647 !important;',
  '}',
  'body { margin: 0 !important; border-color: transparent !important; }',
].join(' ')

/**
 * Walks the DOM tree to find the first element with a non-transparent
 * background color, then sets it on <html> so the Chromium compositor
 * edge (between the viewport and the window frame) blends seamlessly
 * with the extension's visual background.
 */
const DETECT_BG_SCRIPT = `(function() {
  function findBgColor(el) {
    var bg = getComputedStyle(el).backgroundColor;
    if (bg && bg !== 'rgba(0, 0, 0, 0)' && bg !== 'transparent') return bg;
    for (var i = 0; i < el.children.length; i++) {
      var c = findBgColor(el.children[i]);
      if (c) return c;
    }
    return null;
  }
  var color = findBgColor(document.documentElement) || '#ffffff';
  document.documentElement.style.setProperty('background', color, 'important');
})()`

export class PopupView extends EventEmitter {
  static POSITION_PADDING = 5

  static BOUNDS = {
    minWidth: 25,
    minHeight: 25,
    maxWidth: 800,
    maxHeight: 600,
  }

  browserWindow?: BrowserWindow
  parent?: Electron.BaseWindow
  extensionId: string

  private anchorRect: PopupAnchorRect
  private destroyed: boolean = false
  private hidden: boolean = true
  private alignment?: string

  /** Preferred size changes are only received in Electron v12+ */
  private usingPreferredSize = supportsPreferredSize()

  private readyPromise: Promise<void>

  constructor(opts: PopupViewOptions) {
    super()

    this.parent = opts.parent
    this.extensionId = opts.extensionId
    this.anchorRect = opts.anchorRect
    this.alignment = opts.alignment

    this.browserWindow = new BrowserWindow({
      show: false,
      frame: false,
      movable: false,
      maximizable: false,
      minimizable: false,
      fullscreenable: false,
      resizable: false,
      skipTaskbar: true,
      transparent: true,
      hasShadow: false,
      roundedCorners: true,
      webPreferences: {
        session: opts.session,
        sandbox: true,
        nodeIntegration: false,
        nodeIntegrationInWorker: false,
        contextIsolation: true,
        enablePreferredSizeMode: true,
      },
    })

    const untypedWebContents = this.browserWindow.webContents as any
    untypedWebContents.on('preferred-size-changed', this.updatePreferredSize)

    this.browserWindow.webContents.on('devtools-closed', this.maybeClose)
    this.browserWindow.on('blur', this.maybeClose)
    this.browserWindow.on('closed', this.destroy)
    this.parent.once('closed', this.destroy)

    this.readyPromise = this.load(opts.url)
  }

  private show() {
    this.hidden = false
    this.browserWindow?.show()
  }

  private async load(url: string): Promise<void> {
    const win = this.browserWindow!

    try {
      await win.webContents.loadURL(url)
    } catch (e) {
      console.error('[Popup] loadURL error:', e)
    }

    if (this.destroyed) return

    await win.webContents.insertCSS(POPUP_CSS)
    await win.webContents.executeJavaScript(DETECT_BG_SCRIPT)

    this.setSize({ width: 400, height: 600 })
    this.updatePosition()
    this.show()
  }

  destroy = () => {
    if (this.destroyed) return

    this.destroyed = true

    d(`destroying ${this.extensionId}`)

    if (this.parent) {
      if (!this.parent.isDestroyed()) {
        this.parent.off('closed', this.destroy)
      }
      this.parent = undefined
    }

    if (this.browserWindow) {
      if (!this.browserWindow.isDestroyed()) {
        const { webContents } = this.browserWindow

        if (!webContents.isDestroyed() && webContents.isDevToolsOpened()) {
          webContents.closeDevTools()
        }

        this.browserWindow.off('closed', this.destroy)
        this.browserWindow.destroy()
      }

      this.browserWindow = undefined
    }
  }

  isDestroyed() {
    return this.destroyed
  }

  whenReady() {
    return this.readyPromise
  }

  setSize(rect: Partial<Electron.Rectangle>) {
    if (!this.browserWindow || !this.parent) return

    const width = Math.floor(
      Math.min(PopupView.BOUNDS.maxWidth, Math.max(rect.width || 0, PopupView.BOUNDS.minWidth)),
    )

    const height = Math.floor(
      Math.min(PopupView.BOUNDS.maxHeight, Math.max(rect.height || 0, PopupView.BOUNDS.minHeight)),
    )

    const size = { width, height }
    d(`setSize`, size)

    this.emit('will-resize', size)

    this.browserWindow?.setBounds({
      ...this.browserWindow.getBounds(),
      ...size,
    })

    this.emit('resized')
  }

  private maybeClose = () => {
    if (!this.browserWindow?.isDestroyed() && this.browserWindow?.webContents.isDevToolsOpened()) {
      return
    }
    setTimeout(() => {
      if (this.destroyed) return
      if (!this.browserWindow?.isDestroyed() && this.browserWindow?.isFocused()) return
      if (!getAllWindows().some((win) => win.isFocused())) return
      this.destroy()
    }, 500)
  }

  private updatePosition() {
    if (!this.browserWindow || !this.parent) return

    const winBounds = this.parent.getBounds()
    const winContentBounds = this.parent.getContentBounds()
    const nativeTitlebarHeight = winBounds.height - winContentBounds.height

    const viewBounds = this.browserWindow.getBounds()

    let x = winBounds.x + this.anchorRect.x + this.anchorRect.width - viewBounds.width
    let y =
      winBounds.y +
      nativeTitlebarHeight +
      this.anchorRect.y +
      this.anchorRect.height +
      PopupView.POSITION_PADDING

    if (this.alignment?.includes('right')) x = winBounds.x + this.anchorRect.x
    if (this.alignment?.includes('top'))
      y =
        winBounds.y +
        nativeTitlebarHeight -
        viewBounds.height +
        this.anchorRect.y -
        PopupView.POSITION_PADDING

    x = Math.floor(x)
    y = Math.floor(y)

    const position = { x, y }
    d(`updatePosition`, position)

    this.emit('will-move', position)

    this.browserWindow.setBounds({
      ...this.browserWindow.getBounds(),
      ...position,
    })

    this.emit('moved')
  }

  private async queryPreferredSize() {
    if (this.usingPreferredSize || this.destroyed) return

    const rect = await this.browserWindow!.webContents.executeJavaScript(
      `((${() => {
        const rect = document.body.getBoundingClientRect()
        return { width: rect.width, height: rect.height }
      }})())`,
    )

    if (this.destroyed) return

    this.setSize({ width: rect.width, height: rect.height })
    this.updatePosition()
  }

  private updatePreferredSize = (event: Electron.Event, size: Electron.Size) => {
    d('updatePreferredSize', size)
    this.usingPreferredSize = true
    this.setSize(size)
    this.updatePosition()

    if (this.hidden) this.show()
  }
}
