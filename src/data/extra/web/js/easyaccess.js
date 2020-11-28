class EasyAccess {
    constructor() {
        // Implement mouse drag with Ctrl and left button pressed to scroll.
        this.lastMouseClientX = 0;
        this.lastMouseClientY = 0;
        this.readyToScroll = false;
        this.scrolled = false;

        // Vi-like navigation.
        // Pending keys for keydown.
        this.pendingKeys = [];
        // The repeat token from user input.
        this.repeatToken =  0;

        window.vnotex.on('ready', () => {
            this.setupMouseMove();
            this.setupViNavigation();
            this.setupZoomOnWheel();
        });
    }

    setupMouseMove() {
        window.addEventListener('mousedown', (e) => {
            e = e || window.event;
            let isCtrl = window.vnotex.os === 'Mac' ? e.metaKey : e.ctrlKey;
            // Left button and Ctrl key.
            if (e.buttons == 1
                && isCtrl
                && window.getSelection().type != 'Range'
                && !window.vxImageViewer.isViewingImage()) {
                this.lastMouseClientX = e.clientX;
                this.lastMouseClientY = e.clientY;
                this.readyToScroll = true;
                this.scrolled = false;
                e.preventDefault();
            } else {
                this.readyToScroll = false;
                this.scrolled = false;
            }
        });

        window.addEventListener('mouseup', (e) => {
            e = e || window.event;
            if (this.scrolled || this.readyToScroll) {
                // Have been scrolled, restore the cursor style.
                document.body.style.cursor = "auto";
                e.preventDefault();
            }

            this.readyToScroll = false;
            this.scrolled = false;
        });

        window.addEventListener('mousemove', (e) => {
            e = e || window.event;
            if (this.readyToScroll) {
                let deltaX = e.clientX - this.lastMouseClientX;
                let deltaY = e.clientY - this.lastMouseClientY;

                let threshold = 5;
                if (Math.abs(deltaX) >= threshold || Math.abs(deltaY) >= threshold) {
                    this.lastMouseClientX = e.clientX;
                    this.lastMouseClientY = e.clientY;

                    if (!this.scrolled) {
                        this.scrolled = true;
                        document.body.style.cursor = "all-scroll";
                    }

                    let scrollX = -deltaX;
                    let scrollY = -deltaY;
                    window.scrollBy(scrollX, scrollY);
                }

                e.preventDefault();
            }
        });
    }

    setupViNavigation() {
        document.addEventListener('keydown', (e) => {
            // Need to clear pending kyes.
            let needClear = true;

            // This event has been handled completely. No need to call the default handler.
            let accepted = true;

            e = e || window.event;
            let key = null;
            let shift = null;
            let ctrl = null;
            let meta = null;
            if (e.which) {
                key = e.which;
            } else {
                key = e.keyCode;
            }

            shift = !!e.shiftKey;
            ctrl = !!e.ctrlKey;
            meta = !!e.metaKey;
            let isCtrl = window.vnotex.os === 'Mac' ? e.metaKey : e.ctrlKey;
            switch (key) {
            // Skip Ctrl, Shift, Alt, Supper.
            case 16:
            case 17:
            case 18:
            case 91:
            case 92:
                needClear = false;
                break;

            // 0 - 9.
            case 48:
            case 49:
            case 50:
            case 51:
            case 52:
            case 53:
            case 54:
            case 55:
            case 56:
            case 57:
            case 96:
            case 97:
            case 98:
            case 99:
            case 100:
            case 101:
            case 102:
            case 103:
            case 104:
            case 105:
            {
                if (this.pendingKeys.length != 0 || ctrl || shift || meta) {
                    accepted = false;
                    break;
                }

                let num = key >= 96 ? key - 96 : key - 48;
                this.repeatToken = this.repeatToken * 10 + num;
                needClear = false;
                break;
            }

            case 74: // J
                if (!shift && (!ctrl || isCtrl) && (!meta || isCtrl)) {
                    EasyAccess.scroll(true);
                    break;
                }

                accepted = false;
                break;

            case 75: // K
                if (!shift && (!ctrl || isCtrl) && (!meta || isCtrl)) {
                    EasyAccess.scroll(false);
                    break;
                }

                accepted = false;
                break;

            case 72: // H
                if (!ctrl && !shift && !meta) {
                    window.scrollBy(-100, 0);
                    break;
                }

                accepted = false;
                break;

            case 76: // L
                if (!ctrl && !shift && !meta) {
                    window.scrollBy(100, 0);
                    break;
                }

                accepted = false;
                break;

            case 71: // G
                if (shift) {
                    if (this.pendingKeys.length == 0) {
                        let scrollLeft = document.documentElement.scrollLeft || document.body.scrollLeft || window.pageXOffset;
                        let scrollHeight = document.documentElement.scrollHeight || document.body.scrollHeight;
                        window.scrollTo(scrollLeft, scrollHeight);
                        break;
                    }
                } else if (!ctrl && !meta) {
                    if (this.pendingKeys.length == 0) {
                        // First g, pend it.
                        this.pendingKeys.push({
                            key: key,
                            ctrl: ctrl,
                            shift: shift
                        });

                        needClear = false;
                        break;
                    } else if (this.pendingKeys.length == 1) {
                        let pendKey = this.pendingKeys[0];
                        if (pendKey.key == key && !pendKey.shift && !pendKey.ctrl) {
                            let scrollLeft = document.documentElement.scrollLeft
                                             || document.body.scrollLeft
                                             || window.pageXOffset;
                            window.scrollTo(scrollLeft, 0);
                            break;
                        }
                    }
                }

                accepted = false;
                break;

            case 85: // U
                if (ctrl) {
                    let clientHeight = document.documentElement.clientHeight;
                    window.scrollBy(0, -clientHeight / 2);
                    break;
                }

                accepted = false;
                break;

            case 68: // D
                if (ctrl) {
                    let clientHeight = document.documentElement.clientHeight;
                    window.scrollBy(0, clientHeight / 2);
                    break;
                }

                accepted = false;
                break;

            case 219: // [ or {
            {
                let repeat = this.repeatToken < 1 ? 1 : this.repeatToken;
                if (shift) {
                    // {
                    if (this.pendingKeys.length == 1) {
                        let pendKey = this.pendingKeys[0];
                        if (pendKey.key == key && !pendKey.shift && !pendKey.ctrl) {
                            // [{, jump to previous title at a higher level.
                            this.jumpTitle(false, -1, repeat);
                            break;
                        }
                    }
                } else if (!ctrl && !meta) {
                    // [
                    if (this.pendingKeys.length == 0) {
                        // First [, pend it.
                        this.pendingKeys.push({
                            key: key,
                            ctrl: ctrl,
                            shift: shift
                        });

                        needClear = false;
                        break;
                    } else if (this.pendingKeys.length == 1) {
                        let pendKey = this.pendingKeys[0];
                        if (pendKey.key == key && !pendKey.shift && !pendKey.ctrl) {
                            // [[, jump to previous title.
                            this.jumpTitle(false, 1, repeat);
                            break;
                        } else if (pendKey.key == 221 && !pendKey.shift && !pendKey.ctrl) {
                            // ][, jump to next title at the same level.
                            this.jumpTitle(true, 0, repeat);
                            break;
                        }
                    }
                }

                accepted = false;
                break;
            }

            case 221: // ] or }
            {
                let repeat = this.repeatToken < 1 ? 1 : this.repeatToken;
                if (shift) {
                    // }
                    if (this.pendingKeys.length == 1) {
                        let pendKey = this.pendingKeys[0];
                        if (pendKey.key == key && !pendKey.shift && !pendKey.ctrl) {
                            // ]}, jump to next title at a higher level.
                            this.jumpTitle(true, -1, repeat);
                            break;
                        }
                    }
                } else if (!ctrl && !meta) {
                    // ]
                    if (this.pendingKeys.length == 0) {
                        // First ], pend it.
                        this.pendingKeys.push({
                            key: key,
                            ctrl: ctrl,
                            shift: shift
                        });

                        needClear = false;
                        break;
                    } else if (this.pendingKeys.length == 1) {
                        let pendKey = this.pendingKeys[0];
                        if (pendKey.key == key && !pendKey.shift && !pendKey.ctrl) {
                            // ]], jump to next title.
                            this.jumpTitle(true, 1, repeat);
                            break;
                        } else if (pendKey.key == 219 && !pendKey.shift && !pendKey.ctrl) {
                            // [], jump to previous title at the same level.
                            this.jumpTitle(false, 0, repeat);
                            break;
                        }
                    }
                }

                accepted = false;
                break;
            }

            default:
                accepted = false;
                break;
            }

            if (needClear) {
                this.repeatToken = 0;
                this.pendingKeys = [];
            }

            if (accepted) {
                e.preventDefault();
            } else {
                window.vnotex.setKeyPress(key, ctrl, shift, meta);
            }
        });
    }

    // @forward: jump forward or backward.
    // @relativeLevel: 0 for the same level as current header;
    //                 negative value for upper level;
    //                 positive value is ignored.
    jumpTitle(forward, relativeLevel, repeat) {
        let headings = window.vnotex.nodeLineMapper.headingNodes;
        if (headings.length == 0) {
            return;
        }

        let currentHeadingIdx = window.vnotex.nodeLineMapper.currentHeadingIndex();
        if (currentHeadingIdx == -1) {
            // At the beginning, before any headings.
            if (relativeLevel < 0 || !forward) {
                return;
            }
        }

        let targetIdx = -1;
        // -1: skip level check.
        let targetLevel = 0;

        let delta = 1;
        if (!forward) {
            delta = -1;
        }

        let scrollTop = document.documentElement.scrollTop || document.body.scrollTop || window.pageYOffset;
        for (targetIdx = (currentHeadingIdx == -1 ? 0 : currentHeadingIdx);
             targetIdx >= 0 && targetIdx < headings.length;
             targetIdx += delta) {
            let level = parseInt(headings[targetIdx].tagName.substr(1));
            if (targetLevel == 0) {
                targetLevel = level;
                if (relativeLevel < 0) {
                    targetLevel += relativeLevel;
                    if (targetLevel < 1) {
                        // Invalid level.
                        return;
                    }
                } else if (relativeLevel > 0) {
                    targetLevel = -1;
                }
            }

            if (targetLevel == -1 || level == targetLevel) {
                if (targetIdx == currentHeadingIdx) {
                    // If current heading is visible, skip it.
                    // Minus 2 to tolerate some margin.
                    if (forward || scrollTop  - 2 <= headings[targetIdx].offsetTop) {
                        continue;
                    }
                }

                if (--repeat == 0) {
                    break;
                }
            } else if (level < targetLevel) {
                return;
            }
        }

        if (targetIdx < 0 || targetIdx >= headings.length) {
            return;
        }

        window.vnotex.nodeLineMapper.scrollToNode(headings[targetIdx], false, false);
        window.setTimeout(function() {
            window.vnotex.nodeLineMapper.updateCurrentHeading();
        }, 1000);
    };

    setupZoomOnWheel() {
        window.addEventListener('wheel', (e) => {
            e = e || window.event;
            let isCtrl = window.vnotex.os === 'Mac' ? e.metaKey : e.ctrlKey;
            if (isCtrl) {
                if (e.deltaY != 0) {
                    window.vnotex.zoom(e.deltaY < 0);
                }
                e.preventDefault();
            }
        });
    }

    static scroll(p_up) {
        let delta = 100;
        if (p_up) {
            window.scrollBy(0, delta);
        } else {
            window.scrollBy(0, -delta);
        }
    }
}

window.vxEasyAccess = new EasyAccess;
