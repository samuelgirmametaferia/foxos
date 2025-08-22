#pragma once
// Minimal text-mode HTML/CSS/JS renderer
// Renders a tiny subset of HTML to the VGA text console.
// Supported:
//  - <h1>, <p>, <br>, <span style="color:..; background:.."> ... </span>
//  - <script> with alert('...') or console.log('...')
//  - Inline style only; colors are basic VGA names or 0..15
// Returns 0 on success, <0 on error
int render_file(const char* path);
