#pragma once
// ViewVideoDetail - adaptive 2-zone layout
// Original features fully preserved from commit 4da495.
// New features added: VP9 quality selector, seek-on-quality-change, English UI,
//                     Comments tab (InnerTube -> yt-dlp fallback, Load More),
//                     Up Next panel (GetRelatedVideos async).
// GUI-FIXES: FA6 icons, volume label, Subscribe fixed-width, title hierarchy,
//            seekbar hit-target, Up Next header.
// GUI-FIXES-2: uniform action-btn widths, consistent PAD on all panels,
//              channel < title font hierarchy, meta line dimmed+separated,
//              status dot tooltip always visible, sidebar item gap+separator,
//              Back btn vertically centred, tab equal padding, view-count
//              uses COL_TEXT_FAINT, related item channel/views use FAINT.
// GUI-FIXES-3: fix PopFont() assert (removed mismatched PushFont/PopFont around
//              title), removed debug red rect around description panel, home
//              btn left-padding restored, vol slider styled consistently with
//              seekbar (thin frame, no thumb square), status dot has proper
//              right-margin before vol block, sidebar search deduplicated
//              (no double search bar in Up Next), btn play width = other btns.
// GUI-FIXES-4: channel name secondary (COL_TEXT_DIM_V4, smaller hit target),
//              meta line explicit margin-top + stronger separator,
//              quality combo FramePadding for arrow breathing room,
//              consistent vol↔quality ItemSpacing, tab underline indicator,
//              FS label shortened to fit flex width.