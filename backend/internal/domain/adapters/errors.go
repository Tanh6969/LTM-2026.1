package adapters

import "errors"

// ErrEmailInUse: email dang ky da thuoc ve mot tai khoan khac.
var ErrEmailInUse = errors.New("email already in use")
