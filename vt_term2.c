/* My idea of a "non naive" terminal would be to simple keep the cursor
 * state in the logical buffer as we parse and then parse only the lines 
 * visible on the screen.
 *
 * But could escape codes from previous, off-screen, lines effect the output
 * of visible lines? Probably yes, but we also have to assume the shell will
 * draw to fit our screen, so maybe there is hope.
 *
 * It can also be said that we won't always be reading from finicky visual
 * TUI programs. If someone just wants to cat a large file (accidentally or 
 * not) they probably just want to see the tail end of the output.
 */

static int vt_ll_get_visual_lines(LogicalLine ll, float cols);

static int
vt_ll_get_visual_lines(LogicalLine ll, float cols) 
{
  /* TODO: make this work for escape codes and unicode */

  if (ll.len == 0) return 0;
  int floor = (int) cols;
  bool rem = (ll.len % floor) > 0;
  return (ll.len/floor) + rem;
}
