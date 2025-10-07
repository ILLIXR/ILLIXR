/*
The MIT Licence
Copyright 2017 © jablonczay.com

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.


 */
;(function ($, undefined) {

    var pluginName = 'codeBoxCopy',
        defaults = {
            tooltipText: 'Copied',
            tooltipShowTime: 1000,
            tooltipFadeInTime: 300,
            tooltipFadeOutTime: 300
        };

    function Plugin( element, options ) {
        this.element = element;
        this.options = $.extend( {}, defaults, options) ;
        this._defaults = defaults;
        this._name = pluginName;
        this.init();
    }

    Plugin.prototype = {

        init: function() {

            var $btn, $tooltip, btn, tooltip, opts, clipboard;

            btn = this.element.querySelector('.code-box-copy__btn');

            if (!btn) return;

            opts = this.options;
            clipboard = new Clipboard(btn);

            clipboard.on('success', function(e) {
                $btn = $(e.trigger);
                $btn.prop('disabled', true);

                tooltip = '<span class="code-box-copy__tooltip">';
                tooltip += opts.tooltipText;
                tooltip += '</span>';
                $(tooltip).prependTo($btn);
                $tooltip = $btn.find('.code-box-copy__tooltip');
                $tooltip.fadeIn(opts.tooltipFadeInTime);

                setTimeout(function () {
                  $tooltip.fadeOut(opts.tooltipFadeOutTime, function () {
                      $tooltip.remove();
                  });
                  $btn.prop('disabled', false);
                }, opts.tooltipShowTime);
            });
        }
    };

    $.fn[pluginName] = function (options) {
        return this.each(function () {
            if (!$.data(this, 'plugin_' + pluginName)) {
                $.data(this, 'plugin_' + pluginName,
                new Plugin(this, options));
            }
        });
    };
})(jQuery);

$(document).ready(function () {
    $('.code-box-copy').codeBoxCopy();
});
