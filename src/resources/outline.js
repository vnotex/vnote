    /**
     * Convert item of header array to html in li 
     * @param item An element of header array
     */
    var itemToHtml = function(item) {
        return '<a href="#' + item.id + '" id="' + 'menu-'+ item.id + '" >' + item.innerText + '</a>';
    };
    /**
     * Generate tree from header array 
     * @param toc_list An array containing header elements
     * @param p_baseLevel The base level number of the toc you want to display
     */
    var tocToTree = function(toc_list, p_baseLevel) {
        let i;
        let p_toc = [];
        for (i in toc_list) {
            let itemLevel = parseInt(toc_list[i].tagName.substring(1));
            if (itemLevel >= p_baseLevel) {
                p_toc.push(toc_list[i]);
            }
        }
        let front = '<li>';
        let ending = ['</li>'];
        let curLevel = p_baseLevel;
        let toclen = p_toc.length;
        for (i in p_toc) {
            let item = p_toc[i];
            console.log(item.tagName);
            let itemLevel = parseInt(item.tagName.substring(1));
            if (item.tagName == curLevel) {
                front += '</li>';
                front += '<li>';
                front += itemToHtml(item);
            } else if (itemLevel > curLevel) {
                // assert(item.level - curLevel == 1)
                front += '<ul>';
                ending.push('</ul>');
                front += '<li>';
                front += itemToHtml(item);
                ending.push('</li>');
                curLevel = itemLevel;
            } else {
                while (itemLevel < curLevel) {
                    let ele = ending.pop();
                    front += ele;
                    if (ele == '</ul>') {
                        curLevel--;
                    }
                }
                front += '</li>';
                front += '<li>';
                front += itemToHtml(item);
            }
        }
        while (ending.length > 0) {
            front += ending.pop();
        }
        front = front.replace("<li></li>", "");
        front = '<ul>' + front + '</ul>';
        return front;
    };

    let headerObjList = $(":header").toArray();
    $('.toc').append(tocToTree( headerObjList, 2 ));


    // scroll to display side outline
    $(window).bind('scroll', function(){
        if ($(document).scrollTop() >= 100) {
            $('.toc').css("display", "block");
            highToc();
        } else {
            $('.toc').css("display", "none");
        }
    });


    // make the corresponding outline text blod
    let highToc = function(){
        $(":header").each(function(index, element) {
            var wst = $(window).scrollTop(); 
            let tag_id = $(this).attr("id");
            if($("#"+tag_id).offset().top <= wst){
                $('.toc a').removeClass("blodtoc");
                $('#menu-'+tag_id).addClass("blodtoc");
            }
        });
    }

    // click to make outline text blod
    $('.toc a').click(function(){
        $('.toc a').removeClass("blodtoc");
        $(this).addClass("blodtoc");
    });

    // button to close the outline
    $('.toc .catalog-close').click(function(){
        $('.toc').hide();
        $(window).unbind('scroll');
    });