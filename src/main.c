/*
 * STC89C52RC / Puzhong 51 A2 calculator.
 *
 * LCD1602:
 *   DB0-DB7 -> P0
 *   RS/RW/E -> P2.6/P2.5/P2.7
 *
 * 5x4 matrix keyboard:
 *   Columns left to right -> P1.0/P1.1/P1.2/P1.3
 *   Rows bottom to top   -> P1.4/P1.5/P1.6/P1.7/P3.2
 *
 *   Backspace AC Percent /
 *   1         2  3       *
 *   4         5  6       -
 *   7         8  9       +
 *   +/-       0  .       =
 */

typedef unsigned char u8;
typedef unsigned int u16;
typedef unsigned long u32;
typedef signed long i32;

__sfr __at (0x80) P0;
__sfr __at (0x90) P1;
__sfr __at (0xA0) P2;
__sfr __at (0xB0) P3;

__sbit __at (0xA6) LCD_RS;
__sbit __at (0xA5) LCD_RW;
__sbit __at (0xA7) LCD_EN;

__sbit __at (0xB2) KEY_ROW_TOP;

#define LCD_COLS        16
#define DISP_BUF_SIZE   24
#define NUM_BUF_SIZE    14

#define KEY_NONE        0xff
#define ACT_SIGN        0x80
#define ACT_BACK        0x81
#define ACT_CLEAR       0x82
#define ACT_PERCENT     0x83
#define ACT_EQUAL       0x84

#define SCALE           100L
#define SCALE_DIGITS    2
#define CALC_MAX        20000000L
#define CALC_MIN        (-20000000L)

static __code const u8 g_key_map[20] = {
    ACT_BACK, ACT_CLEAR, ACT_PERCENT, '/',
    '1', '2', '3', '*',
    '4', '5', '6', '-',
    '7', '8', '9', '+',
    ACT_SIGN, '0', '.', ACT_EQUAL
};

static __code const u16 g_frac_place[2] = { 10, 1 };

static __idata char g_expr[DISP_BUF_SIZE];
static __idata char g_num[NUM_BUF_SIZE];
static char g_rev[8];

static i32 g_left;
static u32 g_edit_abs;

static u8 g_op;
static u8 g_frac_count;
static u8 g_error;

static __bit g_edit_neg;
static __bit g_has_digit;
static __bit g_dot;
static __bit g_result_shown;

static void delay_ms(u16 ms)
{
    u16 i;
    volatile u8 j;

    j = 0;
    while (ms--) {
        for (i = 0; i < 110; i++) {
            j++;
        }
    }
}

static u8 str_len(const char *s)
{
    u8 n;

    n = 0;
    while (s[n] != '\0') {
        n++;
    }
    return n;
}

static void append_char(char *dst, char c, u8 size)
{
    u8 n;

    n = str_len(dst);
    if (n < (u8)(size - 1)) {
        dst[n] = c;
        dst[n + 1] = '\0';
    }
}

static void append_text(char *dst, const char *src, u8 size)
{
    u8 n;
    u8 i;

    n = str_len(dst);
    i = 0;
    while ((src[i] != '\0') && (n < (u8)(size - 1))) {
        dst[n] = src[i];
        n++;
        i++;
    }
    dst[n] = '\0';
}

static void lcd_pulse(void)
{
    LCD_EN = 1;
    delay_ms(1);
    LCD_EN = 0;
    delay_ms(1);
}

static void lcd_cmd(u8 cmd)
{
    LCD_RS = 0;
    LCD_RW = 0;
    P0 = cmd;
    lcd_pulse();
    if ((cmd == 0x01) || (cmd == 0x02)) {
        delay_ms(3);
    }
}

static void lcd_data(u8 dat)
{
    LCD_RS = 1;
    LCD_RW = 0;
    P0 = dat;
    lcd_pulse();
}

static void lcd_set_cursor(u8 row, u8 col)
{
    if (row == 0) {
        lcd_cmd((u8)(0x80 + col));
    } else {
        lcd_cmd((u8)(0xC0 + col));
    }
}

static void lcd_init(void)
{
    LCD_RS = 0;
    LCD_RW = 0;
    LCD_EN = 0;
    P0 = 0xFF;
    delay_ms(50);

    lcd_cmd(0x38);
    delay_ms(5);
    lcd_cmd(0x38);
    delay_ms(1);
    lcd_cmd(0x38);
    delay_ms(1);
    lcd_cmd(0x0C);
    lcd_cmd(0x06);
    lcd_cmd(0x01);
}

static void lcd_line_tail(u8 row, const char *s)
{
    u8 len;
    u8 start;
    u8 i;

    len = str_len(s);
    start = 0;
    if (len > LCD_COLS) {
        start = (u8)(len - LCD_COLS);
        len = LCD_COLS;
    }

    lcd_set_cursor(row, 0);
    for (i = 0; i < LCD_COLS; i++) {
        if (i < len) {
            lcd_data((u8)s[start + i]);
        } else {
            lcd_data(' ');
        }
    }
}

static void lcd_line_right(u8 row, const char *s)
{
    u8 len;
    u8 start;
    u8 pad;
    u8 i;

    len = str_len(s);
    start = 0;
    if (len > LCD_COLS) {
        start = (u8)(len - LCD_COLS);
        len = LCD_COLS;
    }
    pad = (u8)(LCD_COLS - len);

    lcd_set_cursor(row, 0);
    for (i = 0; i < pad; i++) {
        lcd_data(' ');
    }
    for (i = 0; i < len; i++) {
        lcd_data((u8)s[start + i]);
    }
}

static u32 abs_i32(i32 v)
{
    if (v < 0) {
        return (u32)(-v);
    }
    return (u32)v;
}

static void append_u32(char *dst, u32 value, u8 size)
{
    u8 i;
    u8 n;

    i = 0;
    do {
        g_rev[i] = (char)('0' + (u8)(value % 10UL));
        value = (u32)(value / 10UL);
        i++;
    } while ((value != 0UL) && (i < sizeof(g_rev)));

    n = str_len(dst);
    while ((i > 0) && (n < (u8)(size - 1))) {
        i--;
        dst[n] = g_rev[i];
        n++;
    }
    dst[n] = '\0';
}

static void format_scaled(u32 abs_value, u8 neg, u8 edit_mode, char *out)
{
    __idata u32 int_part;
    u32 frac;
    __idata u8 d0;
    __idata u8 d1;
    u8 len;

    out[0] = '\0';
    if ((neg != 0) && (abs_value != 0UL)) {
        append_char(out, '-', NUM_BUF_SIZE);
    }

    int_part = (u32)(abs_value / (u32)SCALE);
    frac = (u32)(abs_value % (u32)SCALE);
    append_u32(out, int_part, NUM_BUF_SIZE);

    d0 = (u8)(frac / 10UL);
    d1 = (u8)(frac % 10UL);

    if (edit_mode != 0) {
        if ((g_dot != 0) || (g_frac_count != 0)) {
            append_char(out, '.', NUM_BUF_SIZE);
            if (g_frac_count > 0) {
                append_char(out, (char)('0' + d0), NUM_BUF_SIZE);
            }
            if (g_frac_count > 1) {
                append_char(out, (char)('0' + d1), NUM_BUF_SIZE);
            }
        }
    } else if (frac != 0UL) {
        append_char(out, '.', NUM_BUF_SIZE);
        append_char(out, (char)('0' + d0), NUM_BUF_SIZE);
        append_char(out, (char)('0' + d1), NUM_BUF_SIZE);
        len = str_len(out);
        while ((len > 0) && (out[len - 1] == '0')) {
            out[len - 1] = '\0';
            len--;
        }
    }
}

static void format_value(i32 value, char *out)
{
    if (value < 0) {
        format_scaled(abs_i32(value), 1, 0, out);
    } else {
        format_scaled((u32)value, 0, 0, out);
    }
}

static void format_edit(char *out)
{
    u8 neg;

    neg = 0;
    if (g_edit_neg != 0) {
        neg = 1;
    }
    format_scaled(g_edit_abs, neg, 1, out);
}

static i32 edit_value(void)
{
    if ((g_edit_neg != 0) && (g_edit_abs != 0UL)) {
        return -(i32)g_edit_abs;
    }
    return (i32)g_edit_abs;
}

static void edit_clear(void)
{
    g_edit_abs = 0;
    g_edit_neg = 0;
    g_has_digit = 0;
    g_dot = 0;
    g_frac_count = 0;
}

static void edit_from_value(i32 value)
{
    g_edit_abs = abs_i32(value);
    g_edit_neg = (value < 0) ? 1 : 0;
    g_has_digit = 1;
    g_dot = 0;
    g_frac_count = 0;
}

static void edit_from_percent_value(i32 value)
{
    u32 frac;

    edit_from_value(value);
    frac = (u32)(g_edit_abs % (u32)SCALE);
    if (frac != 0UL) {
        g_dot = 1;
        if ((frac % 10UL) == 0UL) {
            g_frac_count = 1;
        } else {
            g_frac_count = SCALE_DIGITS;
        }
    }
}

static void clear_all(void)
{
    g_left = 0;
    g_op = 0;
    g_result_shown = 0;
    g_error = 0;
    edit_from_value(0);
}

static void set_error(u8 code)
{
    g_error = code;
    g_result_shown = 0;
}

static u8 add_abs_checked(u32 *sum, u32 term)
{
    if (term > ((u32)CALC_MAX - *sum)) {
        return 0;
    }
    *sum = (u32)(*sum + term);
    return 1;
}

static u8 fixed_add(i32 a, i32 b, i32 *out)
{
    if ((b > 0) && (a > (i32)(CALC_MAX - b))) {
        return 0;
    }
    if ((b < 0) && (a < (i32)(CALC_MIN - b))) {
        return 0;
    }
    *out = (i32)(a + b);
    return 1;
}

static u8 fixed_mul(i32 a, i32 b, i32 *out)
{
    __idata u8 neg;
    __idata u32 ua;
    __idata u32 ub;
    __idata u32 ai;
    __idata u32 af;
    __idata u32 bi;
    __idata u32 bf;
    __idata u32 sum;
    __idata u32 term;
    __idata u32 temp;

    neg = 0;
    if (a < 0) {
        neg = (u8)!neg;
    }
    if (b < 0) {
        neg = (u8)!neg;
    }

    ua = abs_i32(a);
    ub = abs_i32(b);
    ai = (u32)(ua / (u32)SCALE);
    af = (u32)(ua % (u32)SCALE);
    bi = (u32)(ub / (u32)SCALE);
    bf = (u32)(ub % (u32)SCALE);
    sum = 0;

    if ((ai != 0UL) && (bi != 0UL)) {
        temp = (u32)(bi * (u32)SCALE);
        if (ai > ((u32)CALC_MAX / temp)) {
            return 0;
        }
        if (!add_abs_checked(&sum, (u32)(ai * temp))) {
            return 0;
        }
    }
    if ((ai != 0UL) && (bf != 0UL)) {
        if (ai > ((u32)CALC_MAX / bf)) {
            return 0;
        }
        if (!add_abs_checked(&sum, (u32)(ai * bf))) {
            return 0;
        }
    }
    if ((bi != 0UL) && (af != 0UL)) {
        if (bi > ((u32)CALC_MAX / af)) {
            return 0;
        }
        if (!add_abs_checked(&sum, (u32)(bi * af))) {
            return 0;
        }
    }
    term = (u32)((af * bf) / (u32)SCALE);
    if (!add_abs_checked(&sum, term)) {
        return 0;
    }

    if (neg != 0) {
        *out = -(i32)sum;
    } else {
        *out = (i32)sum;
    }
    return 1;
}

static u8 fixed_div(i32 a, i32 b, i32 *out)
{
    u8 neg;
    __idata u32 ua;
    __idata u32 ub;
    __idata u32 result;

    if (b == 0) {
        set_error(1);
        return 0;
    }

    neg = 0;
    if (a < 0) {
        neg = (u8)!neg;
    }
    if (b < 0) {
        neg = (u8)!neg;
    }

    ua = abs_i32(a);
    ub = abs_i32(b);
    result = (u32)((ua * (u32)SCALE) / ub);

    if (result > (u32)CALC_MAX) {
        return 0;
    }
    if (neg != 0) {
        *out = -(i32)result;
    } else {
        *out = (i32)result;
    }
    return 1;
}

static u8 compute(i32 a, i32 b, u8 op, i32 *out)
{
    if (op == '+') {
        return fixed_add(a, b, out);
    }
    if (op == '-') {
        return fixed_add(a, (i32)(-b), out);
    }
    if (op == '*') {
        return fixed_mul(a, b, out);
    }
    if (op == '/') {
        return fixed_div(a, b, out);
    }
    return 0;
}

static void start_number_after_result(void)
{
    if ((g_result_shown != 0) && (g_op == 0)) {
        g_left = 0;
        g_result_shown = 0;
        edit_clear();
    }
}

static void input_digit(u8 digit)
{
    u32 add;

    if (g_error != 0) {
        clear_all();
    }
    start_number_after_result();

    if (g_dot == 0) {
        add = (u32)((u32)(digit - '0') * (u32)SCALE);
        if (g_edit_abs > (u32)(((u32)CALC_MAX - add) / 10UL)) {
            set_error(2);
            return;
        }
        g_edit_abs = (u32)(g_edit_abs * 10UL + add);
    } else if (g_frac_count < SCALE_DIGITS) {
        add = (u32)((u32)(digit - '0') * (u32)g_frac_place[g_frac_count]);
        if (add > ((u32)CALC_MAX - g_edit_abs)) {
            set_error(2);
            return;
        }
        g_edit_abs = (u32)(g_edit_abs + add);
        g_frac_count++;
    }
    g_has_digit = 1;
    g_result_shown = 0;
}

static void input_dot(void)
{
    if (g_error != 0) {
        clear_all();
    }
    start_number_after_result();
    g_dot = 1;
    g_result_shown = 0;
}

static void input_sign(void)
{
    if (g_error != 0) {
        clear_all();
    }
    if ((g_result_shown != 0) && (g_op == 0)) {
        g_left = (i32)(-g_left);
        edit_from_percent_value(g_left);
        g_result_shown = 0;
        return;
    }
    g_edit_neg = (u8)!g_edit_neg;
    g_result_shown = 0;
}

static u8 has_edit_input(void)
{
    return (u8)((g_has_digit != 0) || (g_dot != 0) || (g_edit_neg != 0));
}

static void input_operator(u8 op)
{
    __idata i32 result;
    __idata i32 right;

    if (g_error != 0) {
        return;
    }

    if (g_op == 0) {
        g_left = edit_value();
    } else if (has_edit_input()) {
        right = edit_value();
        if (!compute(g_left, right, g_op, &result)) {
            if (g_error == 0) {
                set_error(2);
            }
            return;
        }
        g_left = result;
    }

    g_op = op;
    g_result_shown = 0;
    edit_clear();
}

static void input_equal(void)
{
    __idata i32 result;
    __idata i32 right;

    if ((g_error != 0) || (g_op == 0) || !has_edit_input()) {
        return;
    }

    right = edit_value();

    if (!compute(g_left, right, g_op, &result)) {
        if (g_error == 0) {
            set_error(2);
        }
        return;
    }

    g_left = result;
    g_op = 0;
    g_result_shown = 1;
    edit_from_value(g_left);
}

static void input_percent(void)
{
    i32 value;

    if (g_error != 0) {
        return;
    }
    if ((g_result_shown != 0) && (g_op == 0)) {
        g_left = (i32)(g_left / 100L);
        edit_from_percent_value(g_left);
        g_result_shown = 0;
        return;
    }

    value = (i32)(edit_value() / 100L);
    edit_from_percent_value(value);
    g_result_shown = 0;
}

static void input_backspace(void)
{
    u32 integer;
    u32 place;
    u32 digit;

    if (g_error != 0) {
        clear_all();
        return;
    }

    if ((g_result_shown != 0) && (g_op == 0)) {
        edit_from_value(g_left);
        g_result_shown = 0;
    }

    if ((g_op != 0) && !has_edit_input()) {
        g_op = 0;
        edit_from_value(g_left);
        return;
    }

    if (g_frac_count > 0) {
        place = (u32)g_frac_place[g_frac_count - 1];
        digit = (u32)((g_edit_abs / place) % 10UL);
        g_edit_abs = (u32)(g_edit_abs - digit * place);
        g_frac_count--;
    } else if (g_dot != 0) {
        g_dot = 0;
    } else if (g_has_digit != 0) {
        integer = (u32)(g_edit_abs / (u32)SCALE);
        integer = (u32)(integer / 10UL);
        g_edit_abs = (u32)(integer * (u32)SCALE);
        if (integer == 0UL) {
            g_has_digit = 0;
        }
    } else {
        g_edit_neg = 0;
    }
}

static void build_live_expr(void)
{
    g_expr[0] = '\0';
    format_value(g_left, g_num);
    append_text(g_expr, g_num, DISP_BUF_SIZE);
    if (g_op != 0) {
        append_char(g_expr, (char)g_op, DISP_BUF_SIZE);
        if (has_edit_input()) {
            format_edit(g_num);
            append_text(g_expr, g_num, DISP_BUF_SIZE);
        }
    }
}

static void render(void)
{
    if (g_error != 0) {
        lcd_line_tail(0, "Error");
        if (g_error == 1) {
            lcd_line_tail(1, "Div by zero");
        } else {
            lcd_line_tail(1, "Overflow");
        }
        return;
    }

    if (g_op != 0) {
        build_live_expr();
        lcd_line_tail(0, g_expr);
        if (has_edit_input()) {
            format_edit(g_num);
        } else {
            g_num[0] = '0';
            g_num[1] = '\0';
        }
        lcd_line_right(1, g_num);
    } else {
        lcd_line_tail(0, "");
        if (g_result_shown != 0) {
            format_value(g_left, g_num);
        } else {
            format_edit(g_num);
        }
        lcd_line_right(1, g_num);
    }
}

static u8 scan_matrix_raw(void)
{
    u8 col;
    u8 row;
    u8 row_bits;
    u8 pattern;

    P1 = 0xF0;
    KEY_ROW_TOP = 1;
    delay_ms(1);
    row_bits = (u8)(P1 & 0xF0);
    if ((row_bits == 0xF0) && (KEY_ROW_TOP != 0)) {
        P1 = 0xFF;
        return KEY_NONE;
    }

    delay_ms(12);
    row_bits = (u8)(P1 & 0xF0);
    if ((row_bits == 0xF0) && (KEY_ROW_TOP != 0)) {
        P1 = 0xFF;
        return KEY_NONE;
    }

    col = KEY_NONE;
    row = KEY_NONE;
    for (pattern = 0xFE; pattern != 0xEF; pattern = (u8)((pattern << 1) | 0x01)) {
        P1 = pattern;
        KEY_ROW_TOP = 1;
        delay_ms(1);

        row_bits = (u8)(P1 & 0xF0);
        if ((row_bits != 0xF0) || (KEY_ROW_TOP == 0)) {
            if (pattern == 0xFE) {
                col = 0;
            } else if (pattern == 0xFD) {
                col = 1;
            } else if (pattern == 0xFB) {
                col = 2;
            } else {
                col = 3;
            }

            if (KEY_ROW_TOP == 0) {
                row = 0;
            } else if (row_bits == 0x70) {
                row = 1;
            } else if (row_bits == 0xB0) {
                row = 2;
            } else if (row_bits == 0xD0) {
                row = 3;
            } else if (row_bits == 0xE0) {
                row = 4;
            }
            break;
        }
    }

    P1 = 0xFF;
    if ((col == KEY_NONE) || (row == KEY_NONE)) {
        return KEY_NONE;
    }

    return (u8)(row * 4 + col);
}

static void wait_matrix_release(void)
{
    u8 guard;

    guard = 0;
    do {
        P1 = 0xF0;
        KEY_ROW_TOP = 1;
        delay_ms(10);
        guard++;
    } while ((((P1 & 0xF0) != 0xF0) || (KEY_ROW_TOP == 0)) && (guard < 200));
    P1 = 0xFF;
}

static u8 scan_key_action(void)
{
    u8 matrix_key;

    matrix_key = scan_matrix_raw();
    if (matrix_key != KEY_NONE) {
        wait_matrix_release();
        return g_key_map[matrix_key];
    }
    return KEY_NONE;
}

static void handle_action(u8 action)
{
    if ((action >= '0') && (action <= '9')) {
        input_digit(action);
    } else if (action == '.') {
        input_dot();
    } else if (action == ACT_SIGN) {
        input_sign();
    } else if ((action == '+') || (action == '-') || (action == '*') || (action == '/')) {
        input_operator(action);
    } else if (action == ACT_BACK) {
        input_backspace();
    } else if (action == ACT_CLEAR) {
        clear_all();
    } else if (action == ACT_PERCENT) {
        input_percent();
    } else if (action == ACT_EQUAL) {
        input_equal();
    }
}

void main(void)
{
    u8 action;

    P0 = 0x00;
    P1 = 0xFF;
    P2 = 0x00;
    P3 = (u8)(P3 | 0x04);

    lcd_init();
    clear_all();
    render();

    while (1) {
        action = scan_key_action();
        if (action != KEY_NONE) {
            handle_action(action);
            render();
        }
    }
}
